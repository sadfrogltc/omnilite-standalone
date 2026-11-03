// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_MEMPOOL_H
#define OMNILITE_STANDALONE_MEMPOOL_H

// Follows the node's mempool for OmniLite. In Litecoin Core, every
// transaction accepted to the mempool goes through TryToAddToMarkerCache
// (validation.cpp, AcceptToMemoryPool); omni_listpendingtransactions lists
// the mempool transactions in that cache. Here the node's mempool is polled:
// new transactions go to the marker cache, the txids to the mempool stand-in
// (coreshim/txmempool.h). Unconfirmed transactions themselves are read from
// the node when asked for, like confirmed ones.

#include <uint256.h>

#include <atomic>
#include <chrono>
#include <set>

class NodeData;

class MempoolWatcher
{
public:
    explicit MempoolWatcher(NodeData& node) : m_node(node) {}

    /** Takes over the node's current mempool. Throws NodeError if the node fails. */
    void Refresh();

    /** Refreshes every interval until stop is set; node errors are logged and retried. */
    void Run(const std::atomic<bool>& stop, std::chrono::seconds interval);

private:
    NodeData& m_node;
    /** Mempool txids already looked at. */
    std::set<uint256> m_known;
};

#endif // OMNILITE_STANDALONE_MEMPOOL_H
