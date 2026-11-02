// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_OMNICORE_UTILSBITCOIN_H
#define OMNILITE_STANDALONE_OMNICORE_UTILSBITCOIN_H

// The network checks of OmniLite's omnicore/utilsbitcoin.h, on the
// parameters selected with SelectParams.

namespace mastercore
{
/** Whether the selected network is mainnet. */
bool MainNet();
/** Whether the selected network is testnet. */
bool TestNet();
/** Whether the selected network is regtest. */
bool RegTest();
/** Whether the selected network is not mainnet. */
bool isNonMainNet();
}

#endif // OMNILITE_STANDALONE_OMNICORE_UTILSBITCOIN_H
