// omnilite-standalone: stands in for Litecoin Core's txmempool.h. The mempool
// is the node's: MempoolWatcher (src/mempool.cpp) keeps a copy of its txids,
// which is all OmniLite asks for.

#ifndef OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H
#define OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H

#include <uint256.h>

#include <mutex>
#include <set>
#include <vector>

class CTxMemPool
{
public:
    void queryHashes(std::vector<uint256>& vtxid) const;
    bool exists(const uint256& hash) const;

    /** Not in Core: replaces the copy with the node's current txids. */
    void SetHashes(const std::set<uint256>& txids);
    /** Not in Core: a transaction this program just broadcast. */
    void Add(const uint256& txid);
    /** Not in Core: drops the transactions of a connected block, as Core's removeForBlock. */
    void RemoveForBlock(const std::vector<uint256>& txids);

private:
    mutable std::mutex m_mutex;
    std::set<uint256> m_txids;
};

extern CTxMemPool mempool;

#endif // OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H
