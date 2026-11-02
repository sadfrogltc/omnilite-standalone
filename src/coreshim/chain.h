// omnilite-standalone: stands in for Litecoin Core's chain.h: index entries
// of the blocks the indexer processed, and the active chain.

#ifndef OMNILITE_STANDALONE_CORESHIM_CHAIN_H
#define OMNILITE_STANDALONE_CORESHIM_CHAIN_H

#include <uint256.h>

#include <cstdint>
#include <vector>

class CBlockIndex
{
public:
    //! pointer to the hash of the block, owned by mapBlockIndex
    const uint256* phashBlock = nullptr;
    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev = nullptr;
    //! height of the entry in the chain
    int nHeight = 0;
    //! block header time
    uint32_t nTime = 0;
    //! number of transactions in the chain up to this block; only OmniLite's progress
    //! report reads it, so it stays 0 here
    unsigned int nChainTx = 0;

    uint256 GetBlockHash() const { return *phashBlock; }
    int64_t GetBlockTime() const { return (int64_t)nTime; }
};

/** An in-memory indexed chain of blocks, as in Core. Heights below the first indexed block are empty. */
class CChain
{
public:
    CBlockIndex* Tip() const { return vChain.size() > 0 ? vChain[vChain.size() - 1] : nullptr; }
    CBlockIndex* operator[](int nHeight) const
    {
        if (nHeight < 0 || nHeight >= (int)vChain.size()) return nullptr;
        return vChain[nHeight];
    }
    bool Contains(const CBlockIndex* pindex) const { return pindex && (*this)[pindex->nHeight] == pindex; }
    int Height() const { return vChain.size() - 1; }
    void SetTip(CBlockIndex* pindex);

private:
    std::vector<CBlockIndex*> vChain;
};

#endif // OMNILITE_STANDALONE_CORESHIM_CHAIN_H
