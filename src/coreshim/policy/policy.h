// omnilite-standalone: stands in for Litecoin Core's policy/policy.h: the dust
// rules OmniLite uses for its outputs.

#ifndef OMNILITE_STANDALONE_CORESHIM_POLICY_POLICY_H
#define OMNILITE_STANDALONE_CORESHIM_POLICY_POLICY_H

#include <amount.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>

/** Min feerate for defining dust (Litecoin Core). */
static const unsigned int DUST_RELAY_TX_FEE = 30000;

CAmount GetDustThreshold(const CTxOut& txout, const CFeeRate& dustRelayFee);
bool IsDust(const CTxOut& txout, const CFeeRate& dustRelayFee);

/** The minimum relay fee (validation.cpp); Litecoin's DEFAULT_MIN_RELAY_TX_FEE unless set. */
extern CFeeRate minRelayTxFee;

#endif // OMNILITE_STANDALONE_CORESHIM_POLICY_POLICY_H
