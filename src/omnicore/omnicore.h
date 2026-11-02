// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_OMNICORE_OMNICORE_H
#define OMNILITE_STANDALONE_OMNICORE_OMNICORE_H

// Excerpt of OmniLite's omnicore/omnicore.h (0.18.1): the constants and
// declarations the ported files use. The in-memory state of the original
// (tally map, locks, wallet totals) is not here; the state follows in phase 3.

#include <omnicore/log.h>

#include <script/standard.h>
#include <uint256.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

class CBlockIndex;
class CMPTransaction;
class CTransaction;
class CTxOut;
class COutPoint;

#define TEST_ECO_PROPERTY_1 (0x80000003UL)

// maximum numeric values from the spec:
#define MAX_INT_8_BYTES (9223372036854775807UL)

// maximum size of string fields
#define SP_STRING_FIELD_LEN 256

// Omni Layer Transaction (Packet) Version
#define MP_TX_PKT_V0  0
#define MP_TX_PKT_V1  1

// Transaction types, from the spec
enum TransactionType {
  MSC_TYPE_SIMPLE_SEND                =  0,
  MSC_TYPE_RESTRICTED_SEND            =  2,
  MSC_TYPE_SEND_TO_OWNERS             =  3,
  MSC_TYPE_SEND_ALL                   =  4,
  MSC_TYPE_SEND_NONFUNGIBLE           =  5,
  MSC_TYPE_SAVINGS_MARK               = 10,
  MSC_TYPE_SAVINGS_COMPROMISED        = 11,
  MSC_TYPE_RATELIMITED_MARK           = 12,
  MSC_TYPE_AUTOMATIC_DISPENSARY       = 15,
  MSC_TYPE_TRADE_OFFER                = 20,
  MSC_TYPE_ACCEPT_OFFER_BTC           = 22,
  MSC_TYPE_NOTIFICATION               = 31,
  MSC_TYPE_OFFER_ACCEPT_A_BET         = 40,
  MSC_TYPE_CREATE_PROPERTY_FIXED      = 50,
  MSC_TYPE_CREATE_PROPERTY_VARIABLE   = 51,
  MSC_TYPE_PROMOTE_PROPERTY           = 52,
  MSC_TYPE_CLOSE_CROWDSALE            = 53,
  MSC_TYPE_CREATE_PROPERTY_MANUAL     = 54,
  MSC_TYPE_GRANT_PROPERTY_TOKENS      = 55,
  MSC_TYPE_REVOKE_PROPERTY_TOKENS     = 56,
  MSC_TYPE_CHANGE_ISSUER_ADDRESS      = 70,
  MSC_TYPE_ENABLE_FREEZING            = 71,
  MSC_TYPE_DISABLE_FREEZING           = 72,
  MSC_TYPE_FREEZE_PROPERTY_TOKENS     = 185,
  MSC_TYPE_UNFREEZE_PROPERTY_TOKENS   = 186,
  MSC_TYPE_ANYDATA                    = 200,
  MSC_TYPE_NONFUNGIBLE_DATA           = 201,
  OMNICORE_MESSAGE_TYPE_DEACTIVATION  = 65533,
  OMNICORE_MESSAGE_TYPE_ACTIVATION    = 65534,
  OMNICORE_MESSAGE_TYPE_ALERT         = 65535
};

#define MSC_PROPERTY_TYPE_INDIVISIBLE             1
#define MSC_PROPERTY_TYPE_DIVISIBLE               2
#define MSC_PROPERTY_TYPE_NONFUNGIBLE             5
#define MSC_PROPERTY_TYPE_INDIVISIBLE_REPLACING   65
#define MSC_PROPERTY_TYPE_DIVISIBLE_REPLACING     66
#define MSC_PROPERTY_TYPE_INDIVISIBLE_APPENDING   129
#define MSC_PROPERTY_TYPE_DIVISIBLE_APPENDING     130

#define PKT_RETURNED_OBJECT    (1000)

#define PKT_ERROR             ( -9000)
#define DEX_ERROR_SELLOFFER   (-10000)
#define DEX_ERROR_ACCEPT      (-20000)
#define DEX_ERROR_PAYMENT     (-30000)
// Smart Properties
#define PKT_ERROR_SP          (-40000)
#define PKT_ERROR_CROWD       (-45000)
// Send To Owners
#define PKT_ERROR_STO         (-50000)
#define PKT_ERROR_SEND        (-60000)
#define PKT_ERROR_TRADEOFFER  (-70000)
#define PKT_ERROR_TOKENS      (-82000)
#define PKT_ERROR_SEND_ALL    (-83000)
#define PKT_ERROR_ANYDATA     (-84000)
#define PKT_ERROR_NFT         (-85000)

#define OMNI_PROPERTY_BTC   0
#define OMNI_PROPERTY_MSC   1
#define OMNI_PROPERTY_TMSC  2

/** Number formatting related functions. */
std::string FormatDivisibleMP(int64_t amount, bool fSign = false);
std::string FormatDivisibleShortMP(int64_t amount);
std::string FormatIndivisibleMP(int64_t amount);
std::string FormatByType(int64_t amount, uint16_t propertyType);
// Note: require initialized state to get divisibility.
std::string FormatMP(uint32_t propertyId, int64_t amount, bool fSign = false);
std::string FormatShortMP(uint32_t propertyId, int64_t amount);

/** omnilite-standalone: replaces ScriptToAsmStr of core_io.h in debug log lines: the script as hex. */
std::string ScriptToAsmStr(const CScript& script, bool fAttemptSighashDecode = false);

/** Returns the Exodus address. */
const CTxDestination ExodusAddress();

/** Returns the marker for class C transactions. */
const std::vector<unsigned char> GetOmMarker();

/**
 * omnilite-standalone: replaces Core's coins view. parseTransaction asks
 * this for the outputs the transaction's inputs spend.
 */
class InputSource
{
public:
    virtual ~InputSource() = default;
    /** Fills out with the spent output; false if it cannot be found. */
    virtual bool GetPrevout(const COutPoint& prevout, CTxOut& out) = 0;
};

/**
 * omnilite-standalone: parseTransaction of omnicore.cpp in read-only mode
 * (ParseTransaction), with the inputs taken from inputs.
 *
 * @return 0 for an Omni transaction, 1 for a Class A transaction without
 *         data (a possible DEx payment), < 0 if not an Omni transaction
 */
int ParseTransaction(const CTransaction& tx, int nBlock, unsigned int idx, CMPTransaction& mptx, InputSource& inputs, unsigned int nTime = 0);

namespace mastercore
{
/** Returns the encoding class, used to embed a payload. */
int GetEncodingClass(const CTransaction& tx, int nBlock);

bool isTestEcosystemProperty(uint32_t propertyId);
bool isMainEcosystemProperty(uint32_t propertyId);

std::string strTransactionType(uint16_t txType);
std::string strMPProperty(uint32_t propertyId);

// From sp.h:
std::string strPropertyType(uint16_t propertyType);
std::string strEcosystem(uint8_t ecosystem);
/** omnilite-standalone: until the state follows in phase 3, every property counts as divisible. */
bool isPropertyDivisible(uint32_t propertyId);
}

#endif // OMNILITE_STANDALONE_OMNICORE_OMNICORE_H
