// omnilite-standalone: stands in for Litecoin Core's coins.h: Coin and the
// part of CCoinsViewCache OmniLite uses as its input cache (FillTxInputCache).

#ifndef OMNILITE_STANDALONE_CORESHIM_COINS_H
#define OMNILITE_STANDALONE_CORESHIM_COINS_H

#include <amount.h>
#include <primitives/transaction.h>

#include <map>

class Coin
{
public:
    //! unspent transaction output
    CTxOut out;
    //! at which height this containing transaction was included in the active block chain
    uint32_t nHeight = 0;
    //! whether containing transaction was a coinbase
    bool fCoinBase = false;

    Coin() {}
    Coin(CTxOut&& outIn, int nHeightIn, bool fCoinBaseIn) : out(std::move(outIn)), nHeight(nHeightIn), fCoinBase(fCoinBaseIn) {}
    Coin(const CTxOut& outIn, int nHeightIn, bool fCoinBaseIn) : out(outIn), nHeight(nHeightIn), fCoinBase(fCoinBaseIn) {}

    bool IsSpent() const { return out.IsNull(); }
};

class CCoinsView
{
public:
    virtual ~CCoinsView() {}
};

/** A cache of coins; here it holds only what is added to it, like OmniLite's view over a dummy backend. */
class CCoinsViewCache : public CCoinsView
{
public:
    explicit CCoinsViewCache(CCoinsView*) {}

    const Coin& AccessCoin(const COutPoint& outpoint) const
    {
        static const Coin coinEmpty;
        auto it = cacheCoins.find(outpoint);
        return it == cacheCoins.end() ? coinEmpty : it->second;
    }

    void AddCoin(const COutPoint& outpoint, Coin&& coin, bool) { cacheCoins[outpoint] = std::move(coin); }

    bool HaveInputs(const CTransaction& tx) const
    {
        if (!tx.IsCoinBase()) {
            for (const CTxIn& txin : tx.vin) {
                if (AccessCoin(txin.prevout).IsSpent()) return false;
            }
        }
        return true;
    }

    CAmount GetValueIn(const CTransaction& tx) const
    {
        if (tx.IsCoinBase()) return 0;
        CAmount nResult = 0;
        for (const CTxIn& txin : tx.vin) nResult += AccessCoin(txin.prevout).out.nValue;
        return nResult;
    }

    unsigned int GetCacheSize() const { return cacheCoins.size(); }

    bool Flush()
    {
        cacheCoins.clear();
        return true;
    }

private:
    std::map<COutPoint, Coin> cacheCoins;
};

#endif // OMNILITE_STANDALONE_CORESHIM_COINS_H
