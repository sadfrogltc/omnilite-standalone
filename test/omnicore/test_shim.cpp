// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/omnicore/test_shim.h>

std::mt19937& TestRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

int GetRandInt(int nMax)
{
    return std::uniform_int_distribution<int>(0, nMax - 1)(TestRng());
}
