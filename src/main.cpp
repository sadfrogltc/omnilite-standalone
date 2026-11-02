// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <config.h>
#include <pubkey.h>
#include <rpcclient.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Line(const std::string& label, const std::string& text)
{
    std::printf("%-12s %s\n", (label + ":").c_str(), text.c_str());
}

/** Prints the node status and reports whether everything needed for indexing is in place. */
bool RunCheck(const Config& cfg)
{
    bool ok = true;
    auto fail = [&](const std::string& label, const std::string& text) {
        ok = false;
        Line(label, "ERROR: " + text);
    };

    Line("RPC", cfg.RPCURL());
    std::pair<std::string, std::string> credentials;
    try {
        credentials = cfg.Credentials();
    } catch (const ConfigError& e) {
        fail("Login", e.what());
        return false;
    }
    RPCClient node(cfg.rpcConnect, cfg.rpcPort, credentials.first, credentials.second);

    try {
        UniValue net = node.Call("getnetworkinfo");
        Line("Node", find_value(net, "subversion").get_str() + " (" + std::to_string(find_value(net, "version").get_int()) + ")");

        UniValue info = node.Call("getblockchaininfo");
        std::string chain = find_value(info, "chain").get_str();
        if (chain == Params().CoreChain()) {
            Line("Chain", chain);
        } else {
            fail("Chain", "node runs \"" + chain + "\", configured network is \"" + Params().NetworkIDString() + "\"");
        }
        int64_t blocks = find_value(info, "blocks").get_int64();
        Line("Height", std::to_string(blocks) + " blocks, " + std::to_string(find_value(info, "headers").get_int64()) + " headers");
        if (find_value(info, "initialblockdownload").isTrue()) {
            char progress[32];
            std::snprintf(progress, sizeof(progress), "%.2f", find_value(info, "verificationprogress").get_real() * 100);
            Line("Sync", std::string("node is still syncing (") + progress + " %)");
        } else {
            Line("Sync", "done");
        }

        try {
            TxIndexStatus tx = GetTxIndexStatus(node);
            if (!tx.enabled) {
                fail("txindex", "off, litecoind needs -txindex=1");
            } else {
                Line("txindex", tx.synced ? "on" : "on, still building");
            }
        } catch (const std::exception& e) {
            fail("txindex", std::string("cannot check: ") + e.what());
        }
        if (find_value(info, "pruned").isTrue()) {
            fail("Pruning", "node is pruned, all blocks from " + std::to_string(Params().OmniGenesisBlock()) + " are needed");
        }
        Line("Omni start", "block " + std::to_string(Params().OmniGenesisBlock()) +
            (blocks >= Params().OmniGenesisBlock() ? " (reached)" : " (node is not there yet)"));
    } catch (const NodeUnauthorized& e) {
        fail("Login", e.what());
    } catch (const std::exception& e) {
        fail("Node", e.what());
    }
    return ok;
}

} // namespace

int main(int argc, char* argv[])
{
    // The secp256k1 context CPubKey::IsFullyValid and friends use.
    ECCVerifyHandle verifyHandle;

    Config cfg;
    try {
        cfg = LoadConfig(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const ConfigError& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 2;
    }
    if (cfg.help) {
        std::cout << HelpMessage();
        return 0;
    }
    if (cfg.check) {
        return RunCheck(cfg) ? 0 : 1;
    }
    std::cerr << "Indexing is not implemented yet in this build; use -check." << std::endl;
    return 1;
}
