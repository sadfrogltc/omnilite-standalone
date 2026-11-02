// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <store.h>

#include <serialize.h>
#include <streams.h>
#include <version.h>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include <algorithm>
#include <filesystem>

// Key layout:
//   'M' name            meta values (schema, network, undofloor)
//   'B' height          block record (8-byte big-endian height)
//   'S' ns '\0' key     state value
//   'U' height          undo record of a block

namespace {

// Changes whenever the stored data changes meaning: older databases must be rebuilt.
// 1: blocks only (phase 1).
// 2: Omni transaction records (phase 2).
const std::string SCHEMA_VERSION = "2";

std::string HeightKey(char prefix, int64_t height)
{
    std::string k(1, prefix);
    for (int shift = 56; shift >= 0; shift -= 8) {
        k.push_back(static_cast<char>((static_cast<uint64_t>(height) >> shift) & 0xff));
    }
    return k;
}

std::string MetaKey(const std::string& name) { return "M" + name; }

void Check(const leveldb::Status& s, const char* what)
{
    if (!s.ok()) throw StoreError(std::string(what) + ": " + s.ToString());
}

std::string SerializeBlock(const BlockInfo& b)
{
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << b.height << b.hash << b.prevHash << b.time;
    return ss.str();
}

BlockInfo DeserializeBlock(const std::string& data)
{
    CDataStream ss(data.data(), data.data() + data.size(), SER_DISK, PROTOCOL_VERSION);
    BlockInfo b;
    ss >> b.height >> b.hash >> b.prevHash >> b.time;
    return b;
}

using UndoRecord = std::map<std::string, std::optional<std::string>>;

std::string SerializeUndo(const UndoRecord& undo)
{
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    WriteCompactSize(ss, undo.size());
    for (const auto& entry : undo) {
        ss << entry.first << static_cast<uint8_t>(entry.second ? 1 : 0) << (entry.second ? *entry.second : std::string());
    }
    return ss.str();
}

UndoRecord DeserializeUndo(const std::string& data)
{
    CDataStream ss(data.data(), data.data() + data.size(), SER_DISK, PROTOCOL_VERSION);
    UndoRecord undo;
    uint64_t n = ReadCompactSize(ss);
    for (uint64_t i = 0; i < n; ++i) {
        std::string key, value;
        uint8_t existed;
        ss >> key >> existed >> value;
        undo[key] = existed ? std::optional<std::string>(value) : std::nullopt;
    }
    return undo;
}

/** Smallest key greater than every key starting with prefix, or "" if there is none. */
std::string PrefixEnd(std::string prefix)
{
    while (!prefix.empty()) {
        unsigned char last = static_cast<unsigned char>(prefix.back());
        if (last < 0xff) {
            prefix.back() = static_cast<char>(last + 1);
            return prefix;
        }
        prefix.pop_back();
    }
    return "";
}

} // namespace

UndoUnavailableError::UndoUnavailableError()
    : StoreError("no undo data for this block (reorg deeper than undodepth), delete the database to rebuild it")
{
}

std::string StateKey(const std::string& ns, const std::string& key)
{
    std::string k = "S" + ns;
    k.push_back('\0');
    return k + key;
}

// --- CStore ---

CStore::CStore(const std::string& path, const std::string& network, int64_t undoDepth)
    : m_undoDepth(undoDepth)
{
    std::filesystem::create_directories(path);
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    Check(leveldb::DB::Open(options, path, &db), "open database");
    m_db.reset(db);

    leveldb::WriteBatch batch;
    for (const auto& want : {std::make_pair(std::string("schema"), SCHEMA_VERSION), std::make_pair(std::string("network"), network)}) {
        std::string have;
        if (!GetRaw(MetaKey(want.first), have)) {
            batch.Put(MetaKey(want.first), want.second);
        } else if (have != want.second) {
            throw StoreError("database " + path + " has " + want.first + " \"" + have + "\", expected \"" +
                want.second + "\"; delete it to rebuild");
        }
    }
    Check(m_db->Write(leveldb::WriteOptions(), &batch), "write meta");
}

CStore::~CStore() = default;

bool CStore::GetRaw(const std::string& key, std::string& value) const
{
    leveldb::Status s = m_db->Get(leveldb::ReadOptions(), key, &value);
    if (s.IsNotFound()) return false;
    Check(s, "read");
    return true;
}

KeyValues CStore::ScanRaw(const std::string& prefix) const
{
    KeyValues result;
    std::unique_ptr<leveldb::Iterator> it(m_db->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        result.emplace_back(it->key().ToString(), it->value().ToString());
    }
    Check(it->status(), "scan");
    return result;
}

std::vector<std::string> CStore::KeysBefore(const std::string& prefix, const std::string& end) const
{
    std::vector<std::string> keys;
    std::unique_ptr<leveldb::Iterator> it(m_db->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix) && it->key().compare(end) < 0; it->Next()) {
        keys.push_back(it->key().ToString());
    }
    Check(it->status(), "scan keys");
    return keys;
}

std::string CStore::GetMeta(const std::string& name) const
{
    std::string v;
    return GetRaw(MetaKey(name), v) ? v : "";
}

std::optional<BlockInfo> CStore::Tip() const
{
    std::unique_ptr<leveldb::Iterator> it(m_db->NewIterator(leveldb::ReadOptions()));
    it->Seek(PrefixEnd("B"));
    if (it->Valid()) {
        it->Prev();
    } else {
        it->SeekToLast();
    }
    Check(it->status(), "read tip");
    if (!it->Valid() || !it->key().starts_with("B")) return std::nullopt;
    return DeserializeBlock(it->value().ToString());
}

std::optional<BlockInfo> CStore::BlockAt(int64_t height) const
{
    std::string v;
    if (!GetRaw(HeightKey('B', height), v)) return std::nullopt;
    return DeserializeBlock(v);
}

bool CStore::Get(const std::string& ns, const std::string& key, std::string& value) const
{
    return GetRaw(StateKey(ns, key), value);
}

KeyValues CStore::Scan(const std::string& ns, const std::string& prefix) const
{
    size_t skip = StateKey(ns, "").size();
    KeyValues result = ScanRaw(StateKey(ns, prefix));
    for (auto& kv : result) kv.first.erase(0, skip);
    return result;
}

std::unique_ptr<CBlockTx> CStore::Begin(const BlockInfo& block)
{
    std::optional<BlockInfo> tip = Tip();
    if (tip && (block.height != tip->height + 1 || block.prevHash != tip->hash)) {
        throw NotOnTipError("block " + std::to_string(block.height) + " " + block.hash +
            " does not extend the tip " + std::to_string(tip->height) + " " + tip->hash);
    }
    return std::make_unique<CBlockTx>(*this, block);
}

BlockInfo CStore::Disconnect()
{
    std::optional<BlockInfo> tip = Tip();
    if (!tip) throw StoreError("the database is empty");
    std::string floor = GetMeta("undofloor");
    std::string data;
    if (floor.empty() || tip->height < std::stoll(floor) || !GetRaw(HeightKey('U', tip->height), data)) {
        throw UndoUnavailableError();
    }
    leveldb::WriteBatch batch;
    for (const auto& entry : DeserializeUndo(data)) {
        if (entry.second) {
            batch.Put(entry.first, *entry.second);
        } else {
            batch.Delete(entry.first);
        }
    }
    batch.Delete(HeightKey('U', tip->height));
    batch.Delete(HeightKey('B', tip->height));
    Check(m_db->Write(leveldb::WriteOptions(), &batch), "disconnect block");
    return *tip;
}

// --- CBlockTx ---

CBlockTx::CBlockTx(CStore& store, BlockInfo block) : m_store(store), m_block(std::move(block)) {}

bool CBlockTx::Get(const std::string& ns, const std::string& key, std::string& value) const
{
    auto it = m_changes.find(StateKey(ns, key));
    if (it != m_changes.end()) {
        if (!it->second) return false;
        value = *it->second;
        return true;
    }
    return m_store.Get(ns, key, value);
}

KeyValues CBlockTx::Scan(const std::string& ns, const std::string& prefix) const
{
    std::string start = StateKey(ns, prefix);
    size_t skip = StateKey(ns, "").size();
    std::map<std::string, std::string> merged;
    for (auto& kv : m_store.ScanRaw(start)) merged.emplace(std::move(kv));
    for (auto it = m_changes.lower_bound(start); it != m_changes.end() && it->first.compare(0, start.size(), start) == 0; ++it) {
        if (it->second) {
            merged[it->first] = *it->second;
        } else {
            merged.erase(it->first);
        }
    }
    KeyValues result;
    result.reserve(merged.size());
    for (auto& kv : merged) result.emplace_back(kv.first.substr(skip), std::move(kv.second));
    return result;
}

void CBlockTx::Change(const std::string& ns, const std::string& key, std::optional<std::string> value)
{
    std::string k = StateKey(ns, key);
    if (!m_undo.count(k)) {
        std::string old;
        m_undo[k] = m_store.GetRaw(k, old) ? std::optional<std::string>(old) : std::nullopt;
    }
    m_changes[k] = std::move(value);
}

void CBlockTx::Put(const std::string& ns, const std::string& key, const std::string& value)
{
    Change(ns, key, value);
}

void CBlockTx::Delete(const std::string& ns, const std::string& key)
{
    Change(ns, key, std::nullopt);
}

void CBlockTx::Commit()
{
    if (m_committed) throw StoreError("block committed twice");
    leveldb::WriteBatch batch;
    for (const auto& change : m_changes) {
        if (change.second) {
            batch.Put(change.first, *change.second);
        } else {
            batch.Delete(change.first);
        }
    }
    batch.Put(HeightKey('B', m_block.height), SerializeBlock(m_block));
    batch.Put(HeightKey('U', m_block.height), SerializeUndo(m_undo));

    // Undo data exists from the very first block on; prune what fell out of the window.
    std::string current = m_store.GetMeta("undofloor");
    int64_t floor = current.empty() ? m_block.height : std::stoll(current);
    int64_t newFloor = std::max(floor, m_block.height - m_store.m_undoDepth + 1);
    for (const std::string& key : m_store.KeysBefore("U", HeightKey('U', newFloor))) {
        batch.Delete(key);
    }
    if (current.empty() || newFloor != floor) batch.Put(MetaKey("undofloor"), std::to_string(newFloor));

    Check(m_store.m_db->Write(leveldb::WriteOptions(), &batch), "commit block");
    m_committed = true;
}
