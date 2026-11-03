// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The dry run before every Omni transaction leaves: what is about to be sent
// goes through OmniLite's own parser and logic, as in the next block, but
// nothing is booked. A transaction OmniLite would reject is not sent, so its
// fee is not burnt for nothing.

#ifndef OMNILITE_STANDALONE_DRYRUN_H
#define OMNILITE_STANDALONE_DRYRUN_H

#include <primitives/transaction.h>

#include <string>

/**
 * Throws a JSONRPCError with OmniLite's code and reason unless OmniLite would
 * see tx as an Omni transaction from sender (and to receiver, if given) and
 * accept it against the current state. Not covered: the order with other
 * unconfirmed transactions, and activations, deactivations and alerts.
 */
void CheckOmniTxBeforeSend(const CTransaction& tx, const std::string& sender, const std::string& receiver);

#endif // OMNILITE_STANDALONE_DRYRUN_H
