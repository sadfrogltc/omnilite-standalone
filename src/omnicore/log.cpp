// Ported from OmniLite 0.18.1, src/omnicore/log.cpp (commit fbc27c1).
// Copyright (c) the Omni Core and OmniLite developers; MIT license.
//
// omnilite-standalone: the debug switches and InitDebugLogLevels as in the
// original; output goes to the program log instead of omnicore.log.

#include <omnicore/log.h>

#include <util/system.h>

#include <string>
#include <vector>

thread_local std::vector<std::string>* g_omniLogCapture = nullptr;

bool msc_debug_parser_data        = 0;
bool msc_debug_parser_readonly    = 0;
//! Print information to potential DEx payments and outputs
bool msc_debug_parser_dex         = 1;
bool msc_debug_parser             = 0;
bool msc_debug_verbose            = 0;
bool msc_debug_verbose2           = 0;
bool msc_debug_verbose3           = 0;
bool msc_debug_vin                = 0;
bool msc_debug_script             = 0;
bool msc_debug_dex                = 1;
bool msc_debug_send               = 1;
bool msc_debug_tokens             = 0;
//! Print information about payloads with non-sequential sequence number
bool msc_debug_spec               = 0;
bool msc_debug_exo                = 0;
bool msc_debug_tally              = 1;
bool msc_debug_sp                 = 1;
bool msc_debug_sto                = 1;
bool msc_debug_txdb               = 0;
bool msc_debug_tradedb            = 1;
bool msc_debug_persistence        = 0;
bool msc_debug_ui                 = 0;
bool msc_debug_pending            = 1;
//! Print transaction fields, when interpreting packets
bool msc_debug_packets            = 1;
//! Print transaction fields, when interpreting packets (in RPC mode)
bool msc_debug_packets_readonly   = 0;
bool msc_debug_walletcache        = 0;
//! Print each line added to consensus hash
bool msc_debug_consensus_hash     = 0;
//! Print consensus hashes for each block when parsing
bool msc_debug_consensus_hash_every_block = 0;
//! Print extra info on alert processing
bool msc_debug_alerts             = 1;
//! Print consensus hashes for each transaction when parsing
bool msc_debug_consensus_hash_every_transaction = 0;
//! Debug fees
bool msc_debug_fees               = 1;
//! Debug the non-fungible tokens database
bool msc_debug_nftdb              = 0;

int LogFilePrint(const std::string& str)
{
    return PrintToLog(str);
}

int ConsolePrint(const std::string& str)
{
    return PrintToConsole("%s", str);
}

void ShrinkDebugLog()
{
}

void InitDebugLogLevels()
{
    if (!gArgs.IsArgSet("-omnidebug")) {
        return;
    }

    const std::vector<std::string>& debugLevels = gArgs.GetArgs("-omnidebug");

    for (std::vector<std::string>::const_iterator it = debugLevels.begin(); it != debugLevels.end(); ++it) {
        if (*it == "parser_data") msc_debug_parser_data = true;
        if (*it == "parser_readonly") msc_debug_parser_readonly = true;
        if (*it == "parser_dex") msc_debug_parser_dex = true;
        if (*it == "parser") msc_debug_parser = true;
        if (*it == "verbose") msc_debug_verbose = true;
        if (*it == "verbose2") msc_debug_verbose2 = true;
        if (*it == "verbose3") msc_debug_verbose3 = true;
        if (*it == "vin") msc_debug_vin = true;
        if (*it == "script") msc_debug_script = true;
        if (*it == "dex") msc_debug_dex = true;
        if (*it == "send") msc_debug_send = true;
        if (*it == "tokens") msc_debug_tokens = true;
        if (*it == "spec") msc_debug_spec = true;
        if (*it == "exo") msc_debug_exo = true;
        if (*it == "tally") msc_debug_tally = true;
        if (*it == "sp") msc_debug_sp = true;
        if (*it == "sto") msc_debug_sto = true;
        if (*it == "txdb") msc_debug_txdb = true;
        if (*it == "tradedb") msc_debug_tradedb = true;
        if (*it == "persistence") msc_debug_persistence = true;
        if (*it == "ui") msc_debug_ui = true;
        if (*it == "pending") msc_debug_pending = true;
        if (*it == "packets") msc_debug_packets = true;
        if (*it == "packets_readonly") msc_debug_packets_readonly = true;
        if (*it == "walletcache") msc_debug_walletcache = true;
        if (*it == "consensus_hash") msc_debug_consensus_hash = true;
        if (*it == "consensus_hash_every_block") msc_debug_consensus_hash_every_block = true;
        if (*it == "alerts") msc_debug_alerts = true;
        if (*it == "consensus_hash_every_transaction") msc_debug_consensus_hash_every_transaction = true;
        if (*it == "fees") msc_debug_fees = true;
        if (*it == "nftdb") msc_debug_nftdb = true;
        if (*it == "none" || *it == "all") {
            bool allDebugState = false;
            if (*it == "all") allDebugState = true;
            msc_debug_parser_data = allDebugState;
            msc_debug_parser_readonly = allDebugState;
            msc_debug_parser_dex = allDebugState;
            msc_debug_parser = allDebugState;
            msc_debug_verbose = allDebugState;
            msc_debug_verbose2 = allDebugState;
            msc_debug_verbose3 = allDebugState;
            msc_debug_vin = allDebugState;
            msc_debug_script = allDebugState;
            msc_debug_dex = allDebugState;
            msc_debug_send = allDebugState;
            msc_debug_tokens = allDebugState;
            msc_debug_spec = allDebugState;
            msc_debug_exo = allDebugState;
            msc_debug_tally = allDebugState;
            msc_debug_sp = allDebugState;
            msc_debug_sto = allDebugState;
            msc_debug_txdb = allDebugState;
            msc_debug_tradedb = allDebugState;
            msc_debug_persistence = allDebugState;
            msc_debug_ui = allDebugState;
            msc_debug_pending = allDebugState;
            msc_debug_packets =  allDebugState;
            msc_debug_packets_readonly =  allDebugState;
            msc_debug_walletcache = allDebugState;
            msc_debug_consensus_hash = allDebugState;
            msc_debug_consensus_hash_every_block = allDebugState;
            msc_debug_alerts = allDebugState;
            msc_debug_consensus_hash_every_transaction = allDebugState;
            msc_debug_fees = allDebugState;
            msc_debug_nftdb = allDebugState;
        }
    }
}
