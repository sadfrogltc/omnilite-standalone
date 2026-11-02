// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Checks that the vendored Litecoin Core code builds and works here.

#include <chainparams.h>
#include <hash.h>
#include <key_io.h>
#include <pubkey.h>
#include <script/standard.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(core_tests)

BOOST_AUTO_TEST_CASE(sha256d)
{
    // Double SHA256 of the empty string.
    const std::string empty;
    uint256 h = Hash(empty.begin(), empty.end());
    BOOST_CHECK_EQUAL(HexStr(h.begin(), h.end()), "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456");
}

BOOST_AUTO_TEST_CASE(addresses)
{
    SelectParams("main");
    // The Exodus address round-trips and is P2PKH.
    CTxDestination exodus = DecodeDestination(Params().ExodusAddress());
    BOOST_CHECK(boost::get<CKeyID>(&exodus) != nullptr);
    BOOST_CHECK_EQUAL(EncodeDestination(exodus), Params().ExodusAddress());

    // P2SH is encoded with the "M" prefix; the legacy "3" form decodes to the same.
    CScriptID id(uint160(ParseHex("a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1")));
    std::string m = EncodeDestination(id);
    BOOST_CHECK_EQUAL(m[0], 'M');
    BOOST_CHECK(DecodeDestination(m) == CTxDestination(id));

    SelectParams("regtest");
    CTxDestination regExodus = DecodeDestination(Params().ExodusAddress());
    BOOST_CHECK(boost::get<CScriptID>(&regExodus) != nullptr);
    SelectParams("main");
}

BOOST_AUTO_TEST_CASE(pubkey_validity)
{
    // The secp256k1 generator is a valid point; x = 5 is not on the curve.
    CPubKey g(ParseHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"));
    BOOST_CHECK(g.IsFullyValid());
    std::vector<unsigned char> bad(33, 0);
    bad[0] = 2;
    bad[32] = 5;
    BOOST_CHECK(!CPubKey(bad).IsFullyValid());
}

BOOST_AUTO_TEST_SUITE_END()
