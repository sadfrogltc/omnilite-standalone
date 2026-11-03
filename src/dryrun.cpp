// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <dryrun.h>

#include <omnicore/dex.h>
#include <omnicore/errors.h>
#include <omnicore/log.h>
#include <omnicore/omnicore.h>
#include <omnicore/rules.h>
#include <omnicore/tally.h>
#include <omnicore/tx.h>
#include <omnicore/utilsbitcoin.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <tinyformat.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <vector>

using namespace mastercore;

// --- the checks of dex.cpp, without booking ---

namespace {

/** DEx_offerCreate up to its booking; available: what the seller could offer. */
int CheckOfferCreate(const std::string& seller, uint32_t propertyId, int64_t amountOffered, int block, int64_t amountDesired,
                     uint8_t paymentWindow, int64_t available, bool replacesOwnOffer)
{
    if (paymentWindow == 0) return (DEX_ERROR_SELLOFFER -101);
    if (amountDesired == 0) return (DEX_ERROR_SELLOFFER -101);
    if (!replacesOwnOffer && DEx_getOffer(seller, propertyId)) return (DEX_ERROR_SELLOFFER -10);
    if (!replacesOwnOffer && IsFeatureActivated(FEATURE_FREEDEX, block) && DEx_hasOffer(seller)) return (DEX_ERROR_SELLOFFER -10);
    if (amountOffered > available) {
        PrintToLog("%s: rejected: sender %s has insufficient balance of property %d [%s < %s]\n", __func__,
                   seller, propertyId, FormatDivisibleMP(available), FormatDivisibleMP(amountOffered));
        return (DEX_ERROR_SELLOFFER -25);
    }
    return amountOffered > 0 ? 0 : DEX_ERROR_SELLOFFER;
}

/** DEx_offerUpdate: the old offer goes, its reserve back to the balance, then a new one. */
int CheckOfferUpdate(const std::string& seller, uint32_t propertyId, int64_t amountOffered, int block, int64_t amountDesired, uint8_t paymentWindow)
{
    if (!DEx_offerExists(seller, propertyId)) return (DEX_ERROR_SELLOFFER -12);
    const int64_t available = GetTokenBalance(seller, propertyId, BALANCE) + GetTokenBalance(seller, propertyId, SELLOFFER_RESERVE);
    return CheckOfferCreate(seller, propertyId, amountOffered, block, amountDesired, paymentWindow, available, true);
}

} // namespace

int CMPTransaction::dryRun_TradeOffer()
{
    const int64_t balance = GetTokenBalance(sender, property, BALANCE);
    switch (version) {
        case MP_TX_PKT_V0:
            if (0 != nValue) {
                if (!DEx_offerExists(sender, property)) {
                    return CheckOfferCreate(sender, property, nValue, block, amount_desired, blocktimelimit, balance, false);
                }
                return CheckOfferUpdate(sender, property, nValue, block, amount_desired, blocktimelimit);
            }
            return DEx_offerExists(sender, property) ? 0 : (PKT_ERROR_TRADEOFFER -49);

        case MP_TX_PKT_V1:
            if (DEx_offerExists(sender, property)) {
                if (CANCEL != subaction && UPDATE != subaction) return (PKT_ERROR_TRADEOFFER -48);
            } else {
                if (NEW != subaction) return (PKT_ERROR_TRADEOFFER -49);
            }
            switch (subaction) {
                case NEW: return CheckOfferCreate(sender, property, nValue, block, amount_desired, blocktimelimit, balance, false);
                case UPDATE: return CheckOfferUpdate(sender, property, nValue, block, amount_desired, blocktimelimit);
                case CANCEL: return 0;
                default: return (PKT_ERROR -999);
            }

        default:
            return (PKT_ERROR -500);
    }
}

int CMPTransaction::dryRun_AcceptOffer()
{
    const CMPOffer* offer = DEx_getOffer(receiver, property);
    if (offer == nullptr) {
        PrintToLog("%s: rejected: no matching sell offer for accept order found\n", __func__);
        return (DEX_ERROR_ACCEPT -15);
    }
    if (DEx_acceptExists(receiver, property, sender)) {
        PrintToLog("%s: rejected: an accept order from this same seller for this same offer already exists\n", __func__);
        return DEX_ERROR_ACCEPT -205;
    }
    if (static_cast<int64_t>(tx_fee_paid) < offer->getMinFee()) {
        PrintToLog("%s: rejected: transaction fee too small [%d < %d]\n", __func__, tx_fee_paid, offer->getMinFee());
        return DEX_ERROR_ACCEPT -105;
    }
    const int64_t remaining = GetTokenBalance(receiver, property, SELLOFFER_RESERVE);
    return std::min<int64_t>(remaining, nValue) > 0 ? 0 : (DEX_ERROR_ACCEPT -10);
}

// --- the dry run ---

namespace {

/** Collects OmniLite's log lines of this thread while it lives. */
struct LogCapture {
    std::vector<std::string> lines;
    LogCapture() { g_omniLogCapture = &lines; }
    ~LogCapture() { g_omniLogCapture = nullptr; }
};

} // namespace

void CheckOmniTxBeforeSend(const CTransaction& tx, const std::string& sender, const std::string& receiver)
{
    LOCK2(cs_main, cs_tally);
    LogCapture capture;
    const std::vector<std::string>& log = capture.lines;

    CMPTransaction mtx;
    const int parsed = ParseTransaction(tx, GetHeight() + 1, 0, mtx, GetTime());
    if (parsed != 0) {
        throw JSONRPCError(PKT_ERROR -2, strprintf("OmniLite would not see this as an Omni transaction (%d). Not sent", parsed));
    }
    if (mtx.getSender() != sender) {
        throw JSONRPCError(PKT_ERROR -2, strprintf("OmniLite would see %s as the sender, not %s. Not sent", mtx.getSender(), sender));
    }
    if (!receiver.empty() && mtx.getReceiver() != receiver) {
        throw JSONRPCError(PKT_ERROR -2, strprintf("OmniLite would see %s as the reference address, not %s. Not sent", mtx.getReceiver(), receiver));
    }

    mtx.unlockLogic();
    mtx.setDryRun();
    const int rc = mtx.interpretPacket();
    if (rc != 0) {
        // OmniLite logs why; its last line saying so is the reason given.
        std::string reason = error_str(rc);
        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            if (it->find("rejected") != std::string::npos || it->find("REJECTED") != std::string::npos || it->find("ERROR") != std::string::npos) {
                reason = *it;
                break;
            }
        }
        throw JSONRPCError(rc, strprintf("OmniLite would reject this transaction (%d): %s. Not sent, no fee paid", rc, reason));
    }
}
