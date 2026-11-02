// omnilite-standalone: stands in for Litecoin Core's core_io.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_CORE_IO_H
#define OMNILITE_STANDALONE_CORESHIM_CORE_IO_H

#include <script/script.h>

#include <string>

/** Only used for debug log lines here: the script as hex. */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode = false);

#endif // OMNILITE_STANDALONE_CORESHIM_CORE_IO_H
