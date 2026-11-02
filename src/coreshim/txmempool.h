// omnilite-standalone: stands in for Litecoin Core's txmempool.h. OmniLite's
// pending list only holds transactions of its own wallet, which this program
// does not have.

#ifndef OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H
#define OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H

#include <uint256.h>

#include <vector>

class CTxMemPool
{
public:
    void queryHashes(std::vector<uint256>& vtxid) const { vtxid.clear(); }
};

extern CTxMemPool mempool;

#endif // OMNILITE_STANDALONE_CORESHIM_TXMEMPOOL_H
