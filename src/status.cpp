// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <status.h>

#include <chainparams.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/time.h>

#include <algorithm>
#include <stdexcept>

#ifndef OMNILITE_STANDALONE_VERSION
#define OMNILITE_STANDALONE_VERSION "unknown"
#endif

SyncStatus::SyncStatus() : m_startTime(GetTime()) {}

void SyncStatus::SetTip(const std::optional<BlockInfo>& tip)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tip = tip;
}

void SyncStatus::SetNodeHeight(int64_t height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeHeight = height;
}

void SyncStatus::NodeFailed(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeFailing = true;
    m_lastError = message;
    m_lastErrorTime = GetTime();
}

void SyncStatus::NodeAnswered()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeFailing = false;
}

void SyncStatus::Disconnected(const BlockInfo& block)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_reorgBlocks;
    m_lastReorgHeight = block.height;
    m_lastReorgTime = GetTime();
}

UniValue SyncStatus::ToJSON() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    UniValue r(UniValue::VOBJ);
    r.pushKV("version", OMNILITE_STANDALONE_VERSION);
    r.pushKV("network", Params().NetworkIDString());
    std::string warmup;
    const bool loading = RPCIsInWarmup(&warmup);
    r.pushKV("loading", loading);
    if (loading) r.pushKV("loadingstatus", warmup);
    r.pushKV("startblock", Params().OmniGenesisBlock());
    if (m_tip) {
        r.pushKV("block", m_tip->height);
        r.pushKV("blockhash", m_tip->hash);
        r.pushKV("blocktime", m_tip->time);
    } else {
        r.pushKV("block", NullUniValue);
    }
    r.pushKV("nodeblock", m_nodeHeight >= 0 ? UniValue(m_nodeHeight) : NullUniValue);
    // Before the Omni start, the index has nothing to do until the node gets there.
    const int64_t indexed = m_tip ? m_tip->height : Params().OmniGenesisBlock() - 1;
    const int64_t behind = m_nodeHeight >= 0 ? std::max<int64_t>(0, m_nodeHeight - indexed) : -1;
    r.pushKV("behind", behind >= 0 ? UniValue(behind) : NullUniValue);
    UniValue synced;
    synced.setBool(!loading && !m_nodeFailing && behind == 0);
    r.pushKV("synced", synced);
    UniValue nodeOk;
    nodeOk.setBool(m_nodeHeight >= 0 && !m_nodeFailing);
    r.pushKV("nodeconnected", nodeOk);
    if (!m_lastError.empty()) {
        UniValue e(UniValue::VOBJ);
        e.pushKV("message", m_lastError);
        e.pushKV("time", m_lastErrorTime);
        UniValue current;
        current.setBool(m_nodeFailing);
        e.pushKV("current", current);
        r.pushKV("lastnodeerror", e);
    }
    r.pushKV("reorgblocks", m_reorgBlocks);
    if (m_lastReorgHeight >= 0) {
        UniValue reorg(UniValue::VOBJ);
        reorg.pushKV("block", m_lastReorgHeight);
        reorg.pushKV("time", m_lastReorgTime);
        r.pushKV("lastreorg", reorg);
    }
    r.pushKV("uptime", GetTime() - m_startTime);
    return r;
}

void SyncStatus::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_startTime = GetTime();
    m_tip.reset();
    m_nodeHeight = -1;
    m_nodeFailing = false;
    m_lastError.clear();
    m_lastErrorTime = 0;
    m_reorgBlocks = 0;
    m_lastReorgHeight = -1;
    m_lastReorgTime = 0;
}

SyncStatus& GetSyncStatus()
{
    static SyncStatus status;
    return status;
}

UniValue omnilite_getstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            RPCHelpMan{"omnilite_getstatus",
                "\nReturns the status of OmniLite Standalone: how far the index is, the node, reorgs.\n"
                "Answers also while the Omni state is loading.\n",
                {},
                RPCResult{
                    "{\n"
                    "  \"version\" : \"x.y.z\",          (string) the program version\n"
                    "  \"network\" : \"main\",           (string) main, test or regtest\n"
                    "  \"loading\" : true|false,       (boolean) whether OmniLite is still loading its state\n"
                    "  \"loadingstatus\" : \"...\",      (string, while loading) what it is doing\n"
                    "  \"startblock\" : n,             (number) the first block with Omni transactions\n"
                    "  \"block\" : n|null,             (number) the last block in the index\n"
                    "  \"blockhash\" : \"hash\",         (string) its hash\n"
                    "  \"blocktime\" : n,              (number) its time\n"
                    "  \"nodeblock\" : n|null,         (number) the node's last block, as last seen\n"
                    "  \"behind\" : n|null,            (number) blocks the index is behind the node\n"
                    "  \"synced\" : true|false,        (boolean) loaded, node reachable and nothing behind\n"
                    "  \"nodeconnected\" : true|false, (boolean) whether the node answered last time\n"
                    "  \"lastnodeerror\" : {           (object, if there was one) the last failed node request\n"
                    "    \"message\" : \"...\", \"time\" : n, \"current\" : true|false\n"
                    "  },\n"
                    "  \"reorgblocks\" : n,            (number) blocks disconnected in reorgs since the start\n"
                    "  \"lastreorg\" : {\"block\" : n, \"time\" : n},  (object, if there was one)\n"
                    "  \"uptime\" : n                  (number) seconds since the start\n"
                    "}\n"
                },
                RPCExamples{
                    HelpExampleCli("omnilite_getstatus", "")
                    + HelpExampleRpc("omnilite_getstatus", "")
                },
            }.ToString());

    return GetSyncStatus().ToJSON();
}
