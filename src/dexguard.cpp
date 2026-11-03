// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <dexguard.h>

#include <omnicore/dex.h>
#include <omnicore/omnicore.h>
#include <omnicore/rules.h>
#include <omnicore/tx.h>
#include <omnicore/utilsbitcoin.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <tinyformat.h>
#include <txmempool.h>
#include <util/time.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <map>
#include <mutex>

using namespace mastercore;

namespace {

std::mutex g_paymentsMutex;
/** Sent payments by accept (seller-property+buyer, as OmniLite's key). */
std::map<std::string, uint256> g_payments;

std::string AcceptKey(const std::string& seller, uint32_t propertyId, const std::string& buyer)
{
    return strprintf("%s-%d+%s", seller, propertyId, buyer);
}

} // namespace

void CheckDExPayment(const CWallet& wallet, const std::string& seller, uint32_t propertyId, const std::string& buyer)
{
    // The window is counted from the node's height: if OmniLite lags behind,
    // the accept may already be gone there.
    const int64_t nodeHeight = wallet.Call("getblockcount").get_int64();
    const int height = GetHeight();
    if (height < nodeHeight) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("OmniLite is at block %d, the node at %d: pay once it has caught up", height, nodeHeight));
    }

    {
        std::lock_guard<std::mutex> lock(g_paymentsMutex);
        auto it = g_payments.find(AcceptKey(seller, propertyId, buyer));
        if (it != g_payments.end()) {
            if (mempool.exists(it->second)) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("A payment for this accept is still unconfirmed: %s", it->second.GetHex()));
            }
            g_payments.erase(it);
        }
    }

    LOCK(cs_tally);
    const CMPAccept* accept = DEx_getAccept(seller, propertyId, buyer);
    if (accept == nullptr) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to load accept offer from the distributed exchange");
    }

    // A payment counts up to and including block accept + window; the
    // earliest it can be mined in is the next one.
    const int64_t lastBlock = accept->getAcceptBlock() + accept->getBlockTimeLimit();
    const int64_t blocksLeft = lastBlock - nodeHeight;
    if (blocksLeft < DEX_PAYMENT_MIN_BLOCKS_LEFT) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Only %d block(s) of the payment window left (it ends with block %d): "
            "the payment might not be mined in time, and the LTC would be lost. Not sent; accept again to pay", std::max<int64_t>(blocksLeft, 0), lastBlock));
    }

    // With free DEx, OmniLite books a payment to a seller for the first token
    // that seller offers; another one (or none, the offer gone) takes the LTC.
    if (IsFeatureActivated(FEATURE_FREEDEX, nodeHeight + 1)) {
        uint32_t forSale = 0;
        if (!DEx_getTokenForSale(seller, forSale)) {
            throw JSONRPCError(RPC_MISC_ERROR, "The seller has no offer anymore: OmniLite would not book the payment, the LTC would be lost");
        }
        if (forSale != propertyId) {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("The seller now offers property %d: OmniLite would book the payment for that offer, not for property %d, and the LTC would be lost", forSale, propertyId));
        }
    }
}

void CheckDExPaymentTx(const CTransaction& tx, const std::string& buyer)
{
    LOCK(cs_tally);
    CMPTransaction mtx;
    const int rc = ParseTransaction(tx, GetHeight() + 1, 0, mtx, GetTime());
    if (rc != 1 || mtx.getEncodingClass() != OMNI_CLASS_A) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("OmniLite would not see this transaction as a DEx payment (%d), the LTC would be lost. Not sent. "
            "On testnet and regtest DEx payments are never seen", rc));
    }
    if (mtx.getSender() != buyer) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("OmniLite would see %s as the payer, not the buyer %s. Not sent", mtx.getSender(), buyer));
    }
}

void DExPaymentSent(const std::string& seller, uint32_t propertyId, const std::string& buyer, const uint256& txid)
{
    std::lock_guard<std::mutex> lock(g_paymentsMutex);
    g_payments[AcceptKey(seller, propertyId, buyer)] = txid;
}
