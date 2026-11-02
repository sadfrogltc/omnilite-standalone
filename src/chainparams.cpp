// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <memory>
#include <stdexcept>

namespace {

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        strNetworkID = "main";
        strCoreChain = "main";
        nRPCPort = 9332;
        base58Prefixes[PUBKEY_ADDRESS] = {48};
        base58Prefixes[SCRIPT_ADDRESS] = {5};
        base58Prefixes[SCRIPT_ADDRESS2] = {50};
        base58Prefixes[SECRET_KEY] = {176};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        bech32_hrp = "ltc";
        nOmniGenesisBlock = 2010500;
        strExodusAddress = "LTceXoduS2cetpWJSe47M25i5oKjEccN1h";
        genesis.nTime = 1317972665;
    }
};

class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        strNetworkID = "test";
        strCoreChain = "test";
        strDataDirSuffix = "testnet4";
        nRPCPort = 19332;
        base58Prefixes[PUBKEY_ADDRESS] = {111};
        base58Prefixes[SCRIPT_ADDRESS] = {196};
        base58Prefixes[SCRIPT_ADDRESS2] = {58};
        base58Prefixes[SECRET_KEY] = {239};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        bech32_hrp = "tltc";
        nOmniGenesisBlock = 101;
        strExodusAddress = "QeVAq4mudsSSvyM6mBkd7cUuPsgBBrLq83";
        genesis.nTime = 1486949366;
    }
};

class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        strNetworkID = "regtest";
        strCoreChain = "regtest";
        strDataDirSuffix = "regtest";
        nRPCPort = 19443;
        base58Prefixes[PUBKEY_ADDRESS] = {111};
        base58Prefixes[SCRIPT_ADDRESS] = {196};
        base58Prefixes[SCRIPT_ADDRESS2] = {58};
        base58Prefixes[SECRET_KEY] = {239};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        bech32_hrp = "rltc";
        nOmniGenesisBlock = 101;
        strExodusAddress = "QMSH3Dp8raecReSw7hBbbCCfDpUGXQUz1m";
        genesis.nTime = 1296688602;
    }
};

std::unique_ptr<CChainParams> globalChainParams;

} // namespace

const CChainParams& Params()
{
    if (!globalChainParams) {
        throw std::logic_error("Params() called before SelectParams()");
    }
    return *globalChainParams;
}

void SelectParams(const std::string& network)
{
    if (network == "main" || network == "mainnet" || network.empty()) {
        globalChainParams.reset(new CMainParams());
    } else if (network == "test" || network == "testnet" || network == "testnet4") {
        globalChainParams.reset(new CTestNetParams());
    } else if (network == "regtest") {
        globalChainParams.reset(new CRegTestParams());
    } else {
        throw std::runtime_error("unknown network \"" + network + "\" (use main, test or regtest)");
    }
}
