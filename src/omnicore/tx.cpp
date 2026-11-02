// Master Protocol transaction code

#include <omnicore/tx.h>

// omnilite-standalone: phase 3 // #include <omnicore/activation.h>
// omnilite-standalone: phase 3 // #include <omnicore/dbspinfo.h>
// omnilite-standalone: phase 3 // #include <omnicore/dbstolist.h>
// omnilite-standalone: phase 3 // #include <omnicore/dbtxlist.h>
// omnilite-standalone: phase 3 // #include <omnicore/dex.h>
#include <omnicore/log.h>
// omnilite-standalone: phase 3 // #include <omnicore/notifications.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/rules.h>
// omnilite-standalone: phase 3 // #include <omnicore/sp.h>
// omnilite-standalone: phase 3 // #include <omnicore/sto.h>
// omnilite-standalone: phase 3 // #include <omnicore/nftdb.h>
// omnilite-standalone: phase 3 // #include <omnicore/utilsbitcoin.h>
// omnilite-standalone: phase 3 // #include <omnicore/version.h>

#include <amount.h>
#include <base58.h>
#include <key_io.h>
// omnilite-standalone: not needed // #include <validation.h>
// omnilite-standalone: not needed // #include <sync.h>
// omnilite-standalone: not needed // #include <util/time.h>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <utility>
#include <vector>

using boost::algorithm::token_compress_on;

using namespace mastercore;

/** Returns a label for the given transaction type. */
std::string mastercore::strTransactionType(uint16_t txType)
{
    switch (txType) {
        case MSC_TYPE_SIMPLE_SEND: return "Simple Send";
        case MSC_TYPE_RESTRICTED_SEND: return "Restricted Send";
        case MSC_TYPE_SEND_TO_OWNERS: return "Send To Owners";
        case MSC_TYPE_SEND_ALL: return "Send All";
        case MSC_TYPE_SEND_NONFUNGIBLE: return "Unique Send";
        case MSC_TYPE_SAVINGS_MARK: return "Savings";
        case MSC_TYPE_SAVINGS_COMPROMISED: return "Savings COMPROMISED";
        case MSC_TYPE_RATELIMITED_MARK: return "Rate-Limiting";
        case MSC_TYPE_AUTOMATIC_DISPENSARY: return "Automatic Dispensary";
        case MSC_TYPE_TRADE_OFFER: return "DEx Sell Offer";
        case MSC_TYPE_ACCEPT_OFFER_BTC: return "DEx Accept Offer";
        case MSC_TYPE_CREATE_PROPERTY_FIXED: return "Create Property - Fixed";
        case MSC_TYPE_CREATE_PROPERTY_VARIABLE: return "Create Property - Variable";
        case MSC_TYPE_PROMOTE_PROPERTY: return "Promote Property";
        case MSC_TYPE_CLOSE_CROWDSALE: return "Close Crowdsale";
        case MSC_TYPE_CREATE_PROPERTY_MANUAL: return "Create Property - Manual";
        case MSC_TYPE_GRANT_PROPERTY_TOKENS: return "Grant Property Tokens";
        case MSC_TYPE_REVOKE_PROPERTY_TOKENS: return "Revoke Property Tokens";
        case MSC_TYPE_CHANGE_ISSUER_ADDRESS: return "Change Issuer Address";
        case MSC_TYPE_ENABLE_FREEZING: return "Enable Freezing";
        case MSC_TYPE_DISABLE_FREEZING: return "Disable Freezing";
        case MSC_TYPE_FREEZE_PROPERTY_TOKENS: return "Freeze Property Tokens";
        case MSC_TYPE_UNFREEZE_PROPERTY_TOKENS: return "Unfreeze Property Tokens";
        case MSC_TYPE_ANYDATA: return "Embed any data";
        case MSC_TYPE_NONFUNGIBLE_DATA: return "Set Non-Fungible Token Data";
        case MSC_TYPE_NOTIFICATION: return "Notification";
        case OMNICORE_MESSAGE_TYPE_ALERT: return "ALERT";
        case OMNICORE_MESSAGE_TYPE_DEACTIVATION: return "Feature Deactivation";
        case OMNICORE_MESSAGE_TYPE_ACTIVATION: return "Feature Activation";

        default: return "* unknown type *";
    }
}

/** Helper to convert class number to string. */
static std::string intToClass(int encodingClass)
{
    switch (encodingClass) {
        case OMNI_CLASS_A:
            return "A";
        case OMNI_CLASS_B:
            return "B";
        case OMNI_CLASS_C:
            return "C";
    }

    return "-";
}

/** Checks whether a pointer to the payload is past it's last position. */
bool CMPTransaction::isOverrun(const char* p)
{
    ptrdiff_t pos = (char*) p - (char*) &pkt;
    return (pos > pkt_size);
}

// -------------------- PACKET PARSING -----------------------

/** Parses the packet or payload. */
bool CMPTransaction::interpret_Transaction()
{
    if (!interpret_TransactionType()) {
        PrintToLog("Failed to interpret type and version\n");
        return false;
    }

    switch (type) {
        case MSC_TYPE_SIMPLE_SEND:
            return interpret_SimpleSend();

        case MSC_TYPE_SEND_TO_OWNERS:
            return interpret_SendToOwners();

        case MSC_TYPE_SEND_ALL:
            return interpret_SendAll();

        case MSC_TYPE_SEND_NONFUNGIBLE:
            return interpret_SendNonFungible();

        case MSC_TYPE_TRADE_OFFER:
            return interpret_TradeOffer();

        case MSC_TYPE_ACCEPT_OFFER_BTC:
            return interpret_AcceptOfferBTC();

        case MSC_TYPE_CREATE_PROPERTY_FIXED:
            return interpret_CreatePropertyFixed();

        case MSC_TYPE_CREATE_PROPERTY_VARIABLE:
            return interpret_CreatePropertyVariable();

        case MSC_TYPE_CLOSE_CROWDSALE:
            return interpret_CloseCrowdsale();

        case MSC_TYPE_CREATE_PROPERTY_MANUAL:
            return interpret_CreatePropertyManaged();

        case MSC_TYPE_GRANT_PROPERTY_TOKENS:
            return interpret_GrantTokens();

        case MSC_TYPE_REVOKE_PROPERTY_TOKENS:
            return interpret_RevokeTokens();

        case MSC_TYPE_CHANGE_ISSUER_ADDRESS:
            return interpret_ChangeIssuer();

        case MSC_TYPE_ENABLE_FREEZING:
            return interpret_EnableFreezing();

        case MSC_TYPE_DISABLE_FREEZING:
            return interpret_DisableFreezing();

        case MSC_TYPE_FREEZE_PROPERTY_TOKENS:
            return interpret_FreezeTokens();

        case MSC_TYPE_UNFREEZE_PROPERTY_TOKENS:
            return interpret_UnfreezeTokens();

        case OMNICORE_MESSAGE_TYPE_DEACTIVATION:
            return interpret_Deactivation();

        case MSC_TYPE_ANYDATA:
            return interpret_AnyData();

        case MSC_TYPE_NONFUNGIBLE_DATA:
            return interpret_NonFungibleData();

        case OMNICORE_MESSAGE_TYPE_ACTIVATION:
            return interpret_Activation();

        case OMNICORE_MESSAGE_TYPE_ALERT:
            return interpret_Alert();
    }

    return false;
}

/** Version and type */
bool CMPTransaction::interpret_TransactionType()
{
    if (pkt_size < 4) {
        return false;
    }
    uint16_t txVersion = 0;
    uint16_t txType = 0;
    memcpy(&txVersion, &pkt[0], 2);
    SwapByteOrder16(txVersion);
    memcpy(&txType, &pkt[2], 2);
    SwapByteOrder16(txType);
    version = txVersion;
    type = txType;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t------------------------------\n");
        PrintToLog("\t         version: %d, class %s\n", txVersion, intToClass(encodingClass));
        PrintToLog("\t            type: %d (%s)\n", txType, strTransactionType(txType));
    }

    return true;
}

/** Tx 1 */
bool CMPTransaction::interpret_SimpleSend()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 3 */
bool CMPTransaction::interpret_SendToOwners()
{
    int expectedSize = (version == MP_TX_PKT_V0) ? 16 : 20;
    if (pkt_size < expectedSize) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    if (version > MP_TX_PKT_V0) {
        memcpy(&distribution_property, &pkt[16], 4);
        SwapByteOrder32(distribution_property);
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t             property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t                value: %s\n", FormatMP(property, nValue));
        if (version > MP_TX_PKT_V1) {
            PrintToLog("\t distributionproperty: %d (%s)\n", distribution_property, strMPProperty(distribution_property));
        }
    }

    return true;
}

/** Tx 4 */
bool CMPTransaction::interpret_SendAll()
{
    if (pkt_size < 5) {
        return false;
    }
    memcpy(&ecosystem, &pkt[4], 1);

    property = ecosystem; // provide a hint for the UI, TODO: better handling!

    // Special case: if can't find the receiver -- assume send to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", (int)ecosystem);
    }

    return true;
}

/** Tx 5 */
bool CMPTransaction::interpret_SendNonFungible()
{
    if (pkt_size < 24) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nonfungible_token_start, &pkt[8], 8);
    SwapByteOrder64(nonfungible_token_start);
    memcpy(&nonfungible_token_end, &pkt[16], 8);
    SwapByteOrder64(nonfungible_token_end);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t      tokenstart: %d\n", nonfungible_token_start);
        PrintToLog("\t        tokenend: %d\n", nonfungible_token_end);
    }

    return true;
}

/** Tx 20 */
bool CMPTransaction::interpret_TradeOffer()
{
    int expectedSize = (version == MP_TX_PKT_V0) ? 33 : 34;
    if (pkt_size < expectedSize) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;
    memcpy(&amount_desired, &pkt[16], 8);
    memcpy(&blocktimelimit, &pkt[24], 1);
    memcpy(&min_fee, &pkt[25], 8);
    if (version > MP_TX_PKT_V0) {
        memcpy(&subaction, &pkt[33], 1);
    }
    SwapByteOrder64(amount_desired);
    SwapByteOrder64(min_fee);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
        PrintToLog("\t  amount desired: %s\n", FormatDivisibleMP(amount_desired));
        PrintToLog("\tblock time limit: %d\n", blocktimelimit);
        PrintToLog("\t         min fee: %s\n", FormatDivisibleMP(min_fee));
        if (version > MP_TX_PKT_V0) {
            PrintToLog("\t      sub-action: %d\n", subaction);
        }
    }

    return true;
}

/** Tx 22 */
bool CMPTransaction::interpret_AcceptOfferBTC()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 50 */
bool CMPTransaction::interpret_CreatePropertyFixed()
{
    if (pkt_size < 25) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 5; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(category)-1)); i++;
    memcpy(subcategory, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(subcategory)-1)); i++;
    memcpy(name, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(name)-1)); i++;
    memcpy(url, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(url)-1)); i++;
    memcpy(data, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(data)-1)); i++;
    memcpy(&nValue, p, 8);
    SwapByteOrder64(nValue);
    p += 8;
    nNewValue = nValue;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t        category: %s\n", category);
        PrintToLog("\t     subcategory: %s\n", subcategory);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
        PrintToLog("\t           value: %s\n", FormatByType(nValue, prop_type));
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 51 */
bool CMPTransaction::interpret_CreatePropertyVariable()
{
    if (pkt_size < 39) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 5; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(category)-1)); i++;
    memcpy(subcategory, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(subcategory)-1)); i++;
    memcpy(name, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(name)-1)); i++;
    memcpy(url, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(url)-1)); i++;
    memcpy(data, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(data)-1)); i++;
    memcpy(&property, p, 4);
    SwapByteOrder32(property);
    p += 4;
    memcpy(&nValue, p, 8);
    SwapByteOrder64(nValue);
    p += 8;
    nNewValue = nValue;
    memcpy(&deadline, p, 8);
    SwapByteOrder64(deadline);
    p += 8;
    memcpy(&early_bird, p++, 1);
    memcpy(&percentage, p++, 1);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t        category: %s\n", category);
        PrintToLog("\t     subcategory: %s\n", subcategory);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
        PrintToLog("\tproperty desired: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t tokens per unit: %s\n", FormatByType(nValue, prop_type));
        PrintToLog("\t        deadline: %d\n", deadline);
        PrintToLog("\tearly bird bonus: %d\n", early_bird);
        PrintToLog("\t    issuer bonus: %d\n", percentage);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 53 */
bool CMPTransaction::interpret_CloseCrowdsale()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 54 */
bool CMPTransaction::interpret_CreatePropertyManaged()
{
    if (pkt_size < 17) {
        return false;
    }
    const char* p = 11 + (char*) &pkt;
    std::vector<std::string> spstr;
    memcpy(&ecosystem, &pkt[4], 1);
    memcpy(&prop_type, &pkt[5], 2);
    SwapByteOrder16(prop_type);
    memcpy(&prev_prop_id, &pkt[7], 4);
    SwapByteOrder32(prev_prop_id);
    for (int i = 0; i < 5; i++) {
        spstr.push_back(std::string(p));
        p += spstr.back().size() + 1;
    }
    int i = 0;
    memcpy(category, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(category)-1)); i++;
    memcpy(subcategory, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(subcategory)-1)); i++;
    memcpy(name, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(name)-1)); i++;
    memcpy(url, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(url)-1)); i++;
    memcpy(data, spstr[i].c_str(), std::min(spstr[i].length(), sizeof(data)-1)); i++;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       ecosystem: %d\n", ecosystem);
        PrintToLog("\t   property type: %d (%s)\n", prop_type, strPropertyType(prop_type));
        PrintToLog("\tprev property id: %d\n", prev_prop_id);
        PrintToLog("\t        category: %s\n", category);
        PrintToLog("\t     subcategory: %s\n", subcategory);
        PrintToLog("\t            name: %s\n", name);
        PrintToLog("\t             url: %s\n", url);
        PrintToLog("\t            data: %s\n", data);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

/** Tx 55 */
bool CMPTransaction::interpret_GrantTokens()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    // Get NFT data, was memo before and previously unused here.
    const char* p = 16 + (char*) &pkt;
    std::string spstr(p);
    memcpy(nonfungible_data, spstr.c_str(), std::min(spstr.length(), sizeof(nonfungible_data)-1));

    // Special case: if can't find the receiver -- assume grant to self!
    if (receiver.empty()) {
        receiver = sender;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
        PrintToLog("\t            data: %s\n", nonfungible_data);
    }

    return true;
}

/** Tx 56 */
bool CMPTransaction::interpret_RevokeTokens()
{
    if (pkt_size < 16) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t           value: %s\n", FormatMP(property, nValue));
    }

    return true;
}

/** Tx 70 */
bool CMPTransaction::interpret_ChangeIssuer()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 71 */
bool CMPTransaction::interpret_EnableFreezing()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 72 */
bool CMPTransaction::interpret_DisableFreezing()
{
    if (pkt_size < 8) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
    }

    return true;
}

/** Tx 185 */
bool CMPTransaction::interpret_FreezeTokens()
{
    if (pkt_size < 37) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    /**
        Note, TX185 is a virtual reference transaction type.
              With virtual reference transactions a hash160 in the payload sets the receiver.
              Reference outputs are ignored.
    **/
    unsigned char address_version;
    uint160 address_hash160;
    memcpy(&address_version, &pkt[16], 1);
    memcpy(&address_hash160, &pkt[17], 20);
    receiver = HashToAddress(address_version, address_hash160);
    if (receiver.empty()) {
        return false;
    }
    CTxDestination recAddress = DecodeDestination(receiver);
    if (!IsValidDestination(recAddress)) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t  value (unused): %s\n", FormatMP(property, nValue));
        PrintToLog("\t         address: %s\n", receiver);
    }

    return true;
}

/** Tx 186 */
bool CMPTransaction::interpret_UnfreezeTokens()
{
    if (pkt_size < 37) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nValue, &pkt[8], 8);
    SwapByteOrder64(nValue);
    nNewValue = nValue;

    /**
        Note, TX186 virtual reference transaction type.
              With virtual reference transactions a hash160 in the payload sets the receiver.
              Reference outputs are ignored.
    **/
    unsigned char address_version;
    uint160 address_hash160;
    memcpy(&address_version, &pkt[16], 1);
    memcpy(&address_hash160, &pkt[17], 20);
    receiver = HashToAddress(address_version, address_hash160);
    if (receiver.empty()) {
        return false;
    }
    CTxDestination recAddress = DecodeDestination(receiver);
    if (!IsValidDestination(recAddress)) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t        property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t  value (unused): %s\n", FormatMP(property, nValue));
        PrintToLog("\t         address: %s\n", receiver);
    }

    return true;
}

/** Tx 200 */
bool CMPTransaction::interpret_AnyData()
{
    if (pkt_size < 4) {
        return false;
    }

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t       data: %s\n", "...");
        PrintToLog("\t   receiver: %s\n", receiver);
    }

    return true;
}

/** Tx 201 */
bool CMPTransaction::interpret_NonFungibleData()
{
    if (pkt_size < 25) {
        return false;
    }
    memcpy(&property, &pkt[4], 4);
    SwapByteOrder32(property);
    memcpy(&nonfungible_token_start, &pkt[8], 8);
    SwapByteOrder64(nonfungible_token_start);
    memcpy(&nonfungible_token_end, &pkt[16], 8);
    SwapByteOrder64(nonfungible_token_end);
    memcpy(&nonfungible_data_type, &pkt[24], 1);

    const char* p = 25 + (char*) &pkt;
    std::string spstr(p);
    memcpy(nonfungible_data, spstr.c_str(), std::min(spstr.length(), sizeof(nonfungible_data)-1));

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t                property: %d (%s)\n", property, strMPProperty(property));
        PrintToLog("\t nonfungible_token_start: %d\n", nonfungible_token_start);
        PrintToLog("\t   nonfungible_token_end: %d\n", nonfungible_token_end);
        PrintToLog("\t                    data: %s\n", nonfungible_data);
    }

    return true;
}

/** Tx 65533 */
bool CMPTransaction::interpret_Deactivation()
{
    if (pkt_size < 6) {
        return false;
    }
    memcpy(&feature_id, &pkt[4], 2);
    SwapByteOrder16(feature_id);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
    }

    return true;
}

/** Tx 65534 */
bool CMPTransaction::interpret_Activation()
{
    if (pkt_size < 14) {
        return false;
    }
    memcpy(&feature_id, &pkt[4], 2);
    SwapByteOrder16(feature_id);
    memcpy(&activation_block, &pkt[6], 4);
    SwapByteOrder32(activation_block);
    memcpy(&min_client_version, &pkt[10], 4);
    SwapByteOrder32(min_client_version);

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      feature id: %d\n", feature_id);
        PrintToLog("\tactivation block: %d\n", activation_block);
        PrintToLog("\t minimum version: %d\n", min_client_version);
    }

    return true;
}

/** Tx 65535 */
bool CMPTransaction::interpret_Alert()
{
    if (pkt_size < 11) {
        return false;
    }

    memcpy(&alert_type, &pkt[4], 2);
    SwapByteOrder16(alert_type);
    memcpy(&alert_expiry, &pkt[6], 4);
    SwapByteOrder32(alert_expiry);

    const char* p = 10 + (char*) &pkt;
    std::string spstr(p);
    memcpy(alert_text, spstr.c_str(), std::min(spstr.length(), sizeof(alert_text)-1));

    if ((!rpcOnly && msc_debug_packets) || msc_debug_packets_readonly) {
        PrintToLog("\t      alert type: %d\n", alert_type);
        PrintToLog("\t    expiry value: %d\n", alert_expiry);
        PrintToLog("\t   alert message: %s\n", alert_text);
    }

    if (isOverrun(p)) {
        PrintToLog("%s(): rejected: malformed string value(s)\n", __func__);
        return false;
    }

    return true;
}

// omnilite-standalone: the logic part (interpretPacket, logicMath_*) of the
// original tx.cpp follows in phase 3.
