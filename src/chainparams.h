// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_CHAINPARAMS_H
#define OMNILITE_STANDALONE_CHAINPARAMS_H

// Replaces Litecoin Core's chainparams.h: only what the vendored address
// code (key_io.cpp) and the indexer need. Values are those of Litecoin Core
// 0.18 (src/chainparams.cpp) and OmniLite 0.18.1 (src/omnicore/rules.cpp).

#include <cstdint>
#include <string>
#include <vector>

namespace Consensus {
/** Core's consensus parameters; OmniLite only passes them on to Core functions. */
struct Params {
};
} // namespace Consensus

/** The genesis block; OmniLite only reads its time. */
struct GenesisBlockInfo {
    uint32_t nTime = 0;
};

class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SCRIPT_ADDRESS2,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        MAX_BASE58_TYPES
    };

    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::string& Bech32HRP() const { return bech32_hrp; }
    /** Network name as in the config file: main, test or regtest. */
    const std::string& NetworkIDString() const { return strNetworkID; }
    /** What litecoind reports as "chain" in getblockchaininfo. */
    const std::string& CoreChain() const { return strCoreChain; }
    /** Subdirectory of the Litecoin data directory (testnet4, regtest). */
    const std::string& DataDirSuffix() const { return strDataDirSuffix; }
    int RPCPort() const { return nRPCPort; }
    const Consensus::Params& GetConsensus() const { return consensus; }
    const GenesisBlockInfo& GenesisBlock() const { return genesis; }

    /** First block in which Omni transactions are parsed (GENESIS_BLOCK). */
    int OmniGenesisBlock() const { return nOmniGenesisBlock; }
    /** The Exodus address marking Class A and B transactions. */
    const std::string& ExodusAddress() const { return strExodusAddress; }

protected:
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::string bech32_hrp;
    std::string strNetworkID;
    std::string strCoreChain;
    std::string strDataDirSuffix;
    int nRPCPort = 0;
    int nOmniGenesisBlock = 0;
    std::string strExodusAddress;
    Consensus::Params consensus;
    GenesisBlockInfo genesis;
};

/** The parameters selected with SelectParams. */
const CChainParams& Params();

/** Selects main, test (also testnet, testnet4) or regtest; throws on others. */
void SelectParams(const std::string& network);

#endif // OMNILITE_STANDALONE_CHAINPARAMS_H
