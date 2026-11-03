// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mempool.h>

#include <indexer.h>
#include <logging.h>
#include <omnicore/omnicore.h>
#include <syncer.h>
#include <txmempool.h>

#include <thread>
#include <vector>

void MempoolWatcher::Refresh()
{
    std::vector<uint256> ids = m_node.GetMempool();
    std::set<uint256> current(ids.begin(), ids.end());

    // Gone (mined, replaced or evicted): out of the marker cache too.
    for (auto it = m_known.begin(); it != m_known.end();) {
        if (current.count(*it)) {
            ++it;
        } else {
            RemoveFromMarkerCache(*it);
            it = m_known.erase(it);
        }
    }

    for (const uint256& txid : current) {
        if (m_known.count(txid)) continue;
        CTransactionRef tx;
        uint256 blockHash;
        try {
            if (!m_node.GetTx(txid, tx, blockHash)) continue; // left the mempool meanwhile; next round
        } catch (const NodeError&) {
            throw;
        } catch (const std::exception& e) {
            // e.g. MWEB data, which Litecoin 0.18 cannot read; no Omni transaction either way.
            LogDebug("mempool transaction %s skipped: %s", txid.GetHex(), e.what());
        }
        if (tx && blockHash.IsNull()) TryToAddToMarkerCache(tx);
        m_known.insert(txid);
    }

    mempool.SetHashes(current);
}

void MempoolWatcher::Run(const std::atomic<bool>& stop, std::chrono::seconds interval)
{
    while (!stop) {
        try {
            Refresh();
        } catch (const NodeError& e) {
            LogWarn("mempool: %s; retrying", e.what());
        }
        auto until = std::chrono::steady_clock::now() + interval;
        while (!stop && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}
