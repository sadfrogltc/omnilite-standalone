// omnilite-standalone: stands in for Litecoin Core's core_io.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_CORE_IO_H
#define OMNILITE_STANDALONE_CORESHIM_CORE_IO_H

#include <amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <univalue.h>

#include <string>

/** Only used for debug log lines here: the script as hex. */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode = false);

/** As Core's core_read.cpp and core_write.cpp. */
bool DecodeHexTx(CMutableTransaction& tx, const std::string& hex_tx, bool try_no_witness = false, bool try_witness = true);
std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags = 0);
UniValue ValueFromAmount(const CAmount& amount);

#endif // OMNILITE_STANDALONE_CORESHIM_CORE_IO_H
