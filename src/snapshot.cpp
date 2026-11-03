// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <snapshot.h>

#include <chainparams.h>
#include <omnicore/consensushash.h>
#include <store.h>
#include <syncer.h>
#include <sync.h>
#include <tar.h>
#include <validation.h>

#include <univalue.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <vector>

#ifndef OMNILITE_STANDALONE_VERSION
#define OMNILITE_STANDALONE_VERSION "unknown"
#endif

namespace fs = std::filesystem;

const char* const SNAPSHOT_MANIFEST = "SNAPSHOT.json";

namespace {

const int SNAPSHOT_FORMAT = 1;

/** LevelDB's lock and log files are not part of the state. */
bool Skipped(const fs::path& p)
{
    const std::string name = p.filename().string();
    return name == "LOCK" || name == "LOG" || name == "LOG.old";
}

std::unique_ptr<CStore> OpenStore(const std::string& path, int64_t undoDepth)
{
    try {
        return std::make_unique<CStore>(path, Params().NetworkIDString(), undoDepth);
    } catch (const StoreError& e) {
        throw SnapshotError("cannot open the index " + path + " (is omnilite-standalone still running?): " + e.what());
    }
}

/** Loads OmniLite on the store and returns its consensus hash at the tip. */
std::string ConsensusHash(NodeData& node, CStore& store, const std::string& omniDir, const OmniOptions& options)
{
    OmniIndexer indexer(node, store);
    indexer.Start(omniDir, options);
    LOCK(cs_main);
    return mastercore::GetConsensusHash().GetHex();
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/** Removes a directory at the end unless told to keep it. */
struct RemoveUnlessKept {
    std::string path;
    bool keep = false;
    ~RemoveUnlessKept()
    {
        std::error_code ec;
        if (!keep) fs::remove_all(path, ec);
    }
};

} // namespace

SnapshotInfo ExportSnapshot(const std::string& netDir, int64_t undoDepth, NodeData& node, const OmniOptions& options,
                            const std::string& file)
{
    const fs::path base(netDir);
    if (!fs::is_directory(base / "index") || !fs::is_directory(base / "omnicore")) {
        throw SnapshotError(netDir + " holds no index (index/ and omnicore/)");
    }

    SnapshotInfo info;
    info.network = Params().NetworkIDString();
    {
        std::unique_ptr<CStore> store = OpenStore((base / "index").string(), undoDepth);
        std::optional<BlockInfo> tip = store->Tip();
        if (!tip) throw SnapshotError("nothing indexed yet");
        info.height = tip->height;
        info.blockHash = tip->hash;
        info.blockTime = tip->time;
        info.consensusHash = ConsensusHash(node, *store, (base / "omnicore").string(), options);
    } // OmniLite and the store are closed: the files are final

    // Names in the archive start with the network, as below <datadir>.
    std::vector<std::pair<std::string, fs::path>> files;
    for (const char* part : {"index", "omnicore"}) {
        for (const fs::directory_entry& e : fs::recursive_directory_iterator(base / part)) {
            if (!e.is_regular_file() || Skipped(e.path())) continue;
            files.emplace_back(info.network + "/" + fs::relative(e.path(), base).generic_string(), e.path());
        }
    }
    std::sort(files.begin(), files.end());
    info.files = files.size();

    UniValue manifest(UniValue::VOBJ);
    manifest.pushKV("format", SNAPSHOT_FORMAT);
    manifest.pushKV("program", std::string("omnilite-standalone ") + OMNILITE_STANDALONE_VERSION);
    manifest.pushKV("network", info.network);
    manifest.pushKV("block", info.height);
    manifest.pushKV("blockhash", info.blockHash);
    manifest.pushKV("blocktime", info.blockTime);
    manifest.pushKV("consensushash", info.consensusHash);
    UniValue list(UniValue::VARR);
    std::vector<std::string> hashes;
    for (const auto& f : files) {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("name", f.first);
        entry.pushKV("size", static_cast<int64_t>(fs::file_size(f.second)));
        hashes.push_back(FileSHA256(f.second.string()));
        entry.pushKV("sha256", hashes.back());
        list.push_back(entry);
    }
    manifest.pushKV("files", list);

    // Written next to the target first, so a broken export leaves no file that looks complete.
    const std::string part = file + ".part";
    try {
        TarWriter tar(part);
        tar.Add(SNAPSHOT_MANIFEST, manifest.write(2) + "\n");
        for (size_t i = 0; i < files.size(); ++i) {
            if (tar.AddFile(files[i].first, files[i].second.string()) != hashes[i]) {
                throw SnapshotError(files[i].second.string() + " changed during the export");
            }
        }
        tar.Finish();
        fs::rename(part, file);
    } catch (...) {
        std::error_code ec;
        fs::remove(part, ec);
        throw;
    }
    return info;
}

SnapshotInfo ImportSnapshot(const std::string& netDir, int64_t undoDepth, NodeData& node, ChainSource& chain,
                            const OmniOptions& options, const std::string& file, const std::string& expectedHash)
{
    if (expectedHash.empty()) throw SnapshotError("the expected consensus hash (-snapshothash) is required");
    const fs::path target(netDir);
    if (fs::exists(target) && !fs::is_empty(target)) {
        throw SnapshotError(netDir + " already holds data; move it away to import a snapshot");
    }
    const fs::path tmp = target.string() + ".importing";
    fs::remove_all(tmp); // left over from an import that was cut off
    fs::create_directories(tmp);
    RemoveUnlessKept cleanup{tmp.string()};

    TarReader tar(file);
    std::string name;
    uint64_t size = 0;
    if (!tar.Next(name, size) || name != SNAPSHOT_MANIFEST || size > (64 << 20)) {
        throw SnapshotError(file + " is not a snapshot (" + SNAPSHOT_MANIFEST + " must come first)");
    }
    std::string text;
    tar.Read([&](const char* data, size_t n) { text.append(data, n); });
    UniValue manifest;
    if (!manifest.read(text) || !manifest.isObject() || find_value(manifest, "format").get_int() != SNAPSHOT_FORMAT) {
        throw SnapshotError("unknown snapshot format");
    }
    SnapshotInfo info;
    info.network = find_value(manifest, "network").get_str();
    info.height = find_value(manifest, "block").get_int64();
    info.blockHash = find_value(manifest, "blockhash").get_str();
    info.blockTime = find_value(manifest, "blocktime").get_int64();
    if (info.network != Params().NetworkIDString()) {
        throw SnapshotError("the snapshot is for " + info.network + ", configured is " + Params().NetworkIDString());
    }

    // Every file as listed, with its SHA-256, nothing else.
    std::map<std::string, std::pair<uint64_t, std::string>> listed;
    const UniValue files = find_value(manifest, "files");
    if (!files.isArray()) throw SnapshotError("unknown snapshot format");
    for (const UniValue& f : files.getValues()) {
        listed[find_value(f, "name").get_str()] = {static_cast<uint64_t>(find_value(f, "size").get_int64()), find_value(f, "sha256").get_str()};
    }
    std::set<std::string> seen;
    const std::string prefix = info.network + "/";
    while (tar.Next(name, size)) {
        auto it = listed.find(name);
        if (it == listed.end() || name.compare(0, prefix.size(), prefix) != 0 || !seen.insert(name).second) {
            throw SnapshotError("unexpected file in the snapshot: " + name);
        }
        if (size != it->second.first) throw SnapshotError("wrong size of " + name);
        const fs::path out = tmp / name.substr(prefix.size());
        fs::create_directories(out.parent_path());
        {
            std::ofstream o(out, std::ios::binary | std::ios::trunc);
            tar.Read([&](const char* data, size_t n) { o.write(data, static_cast<std::streamsize>(n)); });
            if (!o) throw SnapshotError("cannot write " + out.string());
        }
        if (FileSHA256(out.string()) != it->second.second) throw SnapshotError("checksum mismatch: " + name + " is damaged");
    }
    if (seen.size() != listed.size()) throw SnapshotError("the snapshot is incomplete");
    info.files = seen.size();

    // The block must be in the node's chain, or the state belongs to another chain.
    const int64_t nodeHeight = chain.GetBlockCount();
    if (nodeHeight < info.height) {
        throw SnapshotError("the node is at block " + std::to_string(nodeHeight) + ", the snapshot at " +
                            std::to_string(info.height) + ": let the node sync first");
    }
    if (chain.GetBlockHash(info.height) != info.blockHash) {
        throw SnapshotError("block " + info.blockHash + " of the snapshot is not in the node's chain");
    }

    {
        std::unique_ptr<CStore> store = OpenStore((tmp / "index").string(), undoDepth);
        std::optional<BlockInfo> tip = store->Tip();
        if (!tip || tip->height != info.height || tip->hash != info.blockHash) {
            throw SnapshotError("the index in the snapshot does not end at its block");
        }
        info.consensusHash = ConsensusHash(node, *store, (tmp / "omnicore").string(), options);
    }
    if (Lower(info.consensusHash) != Lower(expectedHash)) {
        throw SnapshotError("the consensus hash of the imported state is " + info.consensusHash + ", expected " +
                            expectedHash + ": not imported");
    }

    if (fs::exists(target)) fs::remove(target); // empty, checked above
    fs::rename(tmp, target);
    cleanup.keep = true;
    return info;
}
