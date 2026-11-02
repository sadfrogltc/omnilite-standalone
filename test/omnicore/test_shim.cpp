// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/omnicore/test_shim.h>

#include <omnicore/parsing.h>

#include <random>

namespace mastercore {
TestCoinsView view;
} // namespace mastercore

/** OmniLite's ParseTransaction, which reads the global coins view. */
int ParseTransaction(const CTransaction& tx, int nBlock, unsigned int idx, CMPTransaction& mptx, unsigned int nTime)
{
    return ParseTransaction(tx, nBlock, idx, mptx, mastercore::view, nTime);
}

std::mt19937& TestRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

int GetRandInt(int nMax)
{
    return std::uniform_int_distribution<int>(0, nMax - 1)(TestRng());
}
