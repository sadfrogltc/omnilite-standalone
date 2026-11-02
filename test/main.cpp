// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE OmniLite Standalone
#include <boost/test/unit_test.hpp>

#include <logging.h>
#include <pubkey.h>

/** Sets up the secp256k1 context for all tests, as main() does, and keeps the log quiet. */
struct ECCSetup {
    ECCVerifyHandle handle;
    ECCSetup() { SetLogLevel(LogLevel::Error); }
};
BOOST_GLOBAL_FIXTURE(ECCSetup);
