// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The program's status for omnilite_getstatus and -status: written by the
// syncer, read by the API without cs_main, so it answers while a block is
// processed and while OmniLite loads its state.

#ifndef OMNILITE_STANDALONE_STATUS_H
#define OMNILITE_STANDALONE_STATUS_H

#include <store.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <univalue.h>

class JSONRPCRequest;

class SyncStatus
{
public:
    SyncStatus();

    /** The index's tip after a block was connected or disconnected; none before the first. */
    void SetTip(const std::optional<BlockInfo>& tip);
    void SetNodeHeight(int64_t height);
    /** A request to the node failed; the syncer retries. */
    void NodeFailed(const std::string& message);
    /** The node answered again. */
    void NodeAnswered();
    /** A block was disconnected in a reorg. */
    void Disconnected(const BlockInfo& block);

    /** The fields of omnilite_getstatus. */
    UniValue ToJSON() const;

    /** Back to the start (tests). */
    void Reset();

private:
    mutable std::mutex m_mutex;
    int64_t m_startTime;
    std::optional<BlockInfo> m_tip;
    int64_t m_nodeHeight = -1;
    bool m_nodeFailing = false;
    std::string m_lastError;
    int64_t m_lastErrorTime = 0;
    int64_t m_reorgBlocks = 0;
    int64_t m_lastReorgHeight = -1;
    int64_t m_lastReorgTime = 0;
};

SyncStatus& GetSyncStatus();

/** omnilite_getstatus; answered by the API itself, outside warmup and cs_main. */
UniValue omnilite_getstatus(const JSONRPCRequest& request);

#endif // OMNILITE_STANDALONE_STATUS_H
