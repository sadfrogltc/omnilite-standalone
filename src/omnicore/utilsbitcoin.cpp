// Ported from OmniLite 0.18.1, src/omnicore/utilsbitcoin.cpp (commit fbc27c1).
// Copyright (c) the Omni Core and OmniLite developers; MIT license.
//
// omnilite-standalone: only the network checks. GetHeight and the block
// index lookups of the original need a full node; the indexer passes the
// block height explicitly.

#include <omnicore/utilsbitcoin.h>

#include <chainparams.h>

namespace mastercore
{
bool MainNet()
{
    return Params().NetworkIDString() == "main";
}

bool TestNet()
{
    return Params().NetworkIDString() == "test";
}

bool RegTest()
{
    return Params().NetworkIDString() == "regtest";
}

// omnilite-standalone: the original also excludes the "unittest" network,
// which does not exist here.
bool isNonMainNet()
{
    return !MainNet();
}

} // namespace mastercore
