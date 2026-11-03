// omnilite-standalone: stands in for Litecoin Core's index/txindex.h. The
// transaction index is the node's (-txindex, checked at start-up), so it is
// always in sync from OmniLite's point of view.

#ifndef OMNILITE_STANDALONE_CORESHIM_INDEX_TXINDEX_H
#define OMNILITE_STANDALONE_CORESHIM_INDEX_TXINDEX_H

#include <memory>

class TxIndex
{
public:
    bool BlockUntilSyncedToCurrentChain() const { return true; }
    /** OmniLite's addition: the byte offset of a transaction in its block,
     *  used to order wallet transactions. Here its index in the block, which
     *  orders the same; 0 if the node does not know it in a block. */
    unsigned int ReadTxPos(const class uint256& txid) const;
};

extern std::unique_ptr<TxIndex> g_txindex;

#endif // OMNILITE_STANDALONE_CORESHIM_INDEX_TXINDEX_H
