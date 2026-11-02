// omnilite-standalone: stands in for Litecoin Core's chainparamsbase.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_CHAINPARAMSBASE_H
#define OMNILITE_STANDALONE_CORESHIM_CHAINPARAMSBASE_H

#include <string>

namespace CBaseChainParams {
const std::string MAIN = "main";
const std::string TESTNET = "test";
const std::string REGTEST = "regtest";
} // namespace CBaseChainParams

#endif // OMNILITE_STANDALONE_CORESHIM_CHAINPARAMSBASE_H
