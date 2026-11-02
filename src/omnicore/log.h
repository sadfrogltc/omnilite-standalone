// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_OMNICORE_LOG_H
#define OMNILITE_STANDALONE_OMNICORE_LOG_H

// Replaces OmniLite's omnicore/log.h: the same flags and PrintToLog, but
// written to the program log at debug level instead of omnicore.log.

#include <logging.h>

#include <string>

extern bool msc_debug_parser_data;
extern bool msc_debug_parser_readonly;
extern bool msc_debug_parser_dex;
extern bool msc_debug_parser;
extern bool msc_debug_verbose;
extern bool msc_debug_verbose2;
extern bool msc_debug_verbose3;
extern bool msc_debug_vin;
extern bool msc_debug_script;
extern bool msc_debug_dex;
extern bool msc_debug_send;
extern bool msc_debug_tokens;
extern bool msc_debug_spec;
extern bool msc_debug_exo;
extern bool msc_debug_tally;
extern bool msc_debug_sp;
extern bool msc_debug_sto;
extern bool msc_debug_txdb;
extern bool msc_debug_tradedb;
extern bool msc_debug_persistence;
extern bool msc_debug_ui;
extern bool msc_debug_pending;
extern bool msc_debug_packets;
extern bool msc_debug_packets_readonly;
extern bool msc_debug_walletcache;
extern bool msc_debug_consensus_hash;
extern bool msc_debug_consensus_hash_every_block;
extern bool msc_debug_alerts;
extern bool msc_debug_consensus_hash_every_transaction;
extern bool msc_debug_fees;
extern bool msc_debug_nftdb;

/** Logs at debug level; OmniLite's messages end with a newline, which is dropped. */
template <typename... Args>
inline int PrintToLog(const char* fmt, const Args&... args)
{
    std::string s = tfm::format(fmt, args...);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    LogLine(LogLevel::Debug, s);
    return static_cast<int>(s.size());
}

inline int PrintToLog(const std::string& s) { return PrintToLog("%s", s); }

#endif // OMNILITE_STANDALONE_OMNICORE_LOG_H
