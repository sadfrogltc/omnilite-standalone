// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H
#define OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H

// What the tests taken from OmniLite (src/omnicore/test) expect from
// Litecoin Core's test setup (test/test_bitcoin.h, coins.h, random.h), so
// they run with as few changes as possible.

#include <chainparams.h>
#include <omnicore/omnicore.h>
#include <primitives/transaction.h>

#include <map>
#include <random>
#include <string>

/** Selects mainnet for each test and restores it afterwards. */
struct BasicTestingSetup {
    BasicTestingSetup() { SelectParams("main"); }
    ~BasicTestingSetup() { SelectParams("main"); }
};

namespace CBaseChainParams {
const std::string MAIN = "main";
const std::string TESTNET = "test";
const std::string REGTEST = "regtest";
} // namespace CBaseChainParams

/** The part of Core's Coin the tests fill in. */
struct Coin {
    CTxOut out;
};

/** Stands in for Core's coins view cache: the spent outputs by outpoint. */
class TestCoinsView : public InputSource
{
public:
    void AddCoin(const COutPoint& outpoint, Coin&& coin, bool) { m_coins[outpoint] = coin.out; }
    bool GetPrevout(const COutPoint& prevout, CTxOut& out) override
    {
        auto it = m_coins.find(prevout);
        if (it == m_coins.end()) return false;
        out = it->second;
        return true;
    }

private:
    std::map<COutPoint, CTxOut> m_coins;
};

namespace mastercore {
/** The coins view parseTransaction reads in OmniLite; here the tests' input source. */
extern TestCoinsView view;
} // namespace mastercore

/** Random number in [0, nMax), like Core's GetRandInt. */
int GetRandInt(int nMax);

/** The random generator the tests shuffle with (std::random_shuffle is deprecated). */
std::mt19937& TestRng();

#endif // OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H
