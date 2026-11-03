// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The guard of DEx payments (omni_senddexpay). A payment is plain LTC: if it
// comes too late, goes to the wrong seller or is not seen by OmniLite, the
// LTC are gone. Paying is always the user's own call; nothing here pays.
// Each check throws a JSONRPCError, so the payment is never built or sent.

#ifndef OMNILITE_STANDALONE_DEXGUARD_H
#define OMNILITE_STANDALONE_DEXGUARD_H

#include <amount.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <string>

class CWallet;

/** Blocks of the payment window that must be left, so the payment is mined in time. */
static const int DEX_PAYMENT_MIN_BLOCKS_LEFT = 3;

/**
 * Before the payment is built: OmniLite is at the node's height, the
 * accept leaves at least DEX_PAYMENT_MIN_BLOCKS_LEFT blocks to be mined in,
 * no payment of ours for the same accept is still unconfirmed, and OmniLite
 * would book it for this property (with free DEx: the seller's offer).
 * Overpaying is refused by OmniLite itself; every payment buys at least
 * one unit (it rounds up).
 */
void CheckDExPayment(const CWallet& wallet, const std::string& seller, uint32_t propertyId, const std::string& buyer);

/**
 * The signed payment, before it is sent, through OmniLite's own parser as in
 * the next block: it must be seen as a DEx payment from the buyer. Testnet
 * and regtest fail here: OmniLite only knows the mainnet Exodus output.
 */
void CheckDExPaymentTx(const CTransaction& tx, const std::string& buyer);

/** Remembers a sent payment until it is confirmed (or gone from the mempool). */
void DExPaymentSent(const std::string& seller, uint32_t propertyId, const std::string& buyer, const uint256& txid);

#endif // OMNILITE_STANDALONE_DEXGUARD_H
