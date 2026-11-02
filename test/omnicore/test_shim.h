// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H
#define OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H

// What the tests taken from OmniLite (src/omnicore/test) expect from
// Litecoin Core's test setup (test/test_bitcoin.h, random.h). The coins view
// and the chain parameters come from src/coreshim.

#include <chainparams.h>
#include <chainparamsbase.h>
#include <coins.h>

#include <random>

/** Selects mainnet for each test and restores it afterwards. */
struct BasicTestingSetup {
    BasicTestingSetup() { SelectParams(CBaseChainParams::MAIN); }
    ~BasicTestingSetup() { SelectParams(CBaseChainParams::MAIN); }
};

/** Random number in [0, nMax), like Core's GetRandInt. */
int GetRandInt(int nMax);

/** The random generator the tests shuffle with (std::random_shuffle is deprecated). */
std::mt19937& TestRng();

#endif // OMNILITE_STANDALONE_TEST_OMNICORE_TEST_SHIM_H
