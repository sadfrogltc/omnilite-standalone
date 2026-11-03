// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpcclient.h>
#include <syncer.h>
#include <test/fakelitecoind.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(rpcclient_tests)

BOOST_AUTO_TEST_CASE(call)
{
    FakeLitecoind fake;
    RPCClient node("127.0.0.1", fake.port, "user", "pass", 10);
    UniValue params(UniValue::VARR);
    params.push_back(1);
    params.push_back("x");
    BOOST_CHECK_EQUAL(node.Call("getblockhash", params).get_str(), "getblockhash/2");
    BOOST_CHECK_EQUAL(node.Call("getblockcount").get_str(), "getblockcount/0");
    BOOST_CHECK_EQUAL(node.Call("path").get_str(), "/");
    BOOST_CHECK_EQUAL(node.ForWallet("my wallet").Call("path").get_str(), "/wallet/my%20wallet");
}

BOOST_AUTO_TEST_CASE(errors)
{
    FakeLitecoind fake;
    RPCClient node("127.0.0.1", fake.port, "user", "pass", 10);
    try {
        node.Call("fail");
        BOOST_ERROR("no exception");
    } catch (const RPCError& e) {
        BOOST_CHECK_EQUAL(e.code, RPC_INVALID_ADDRESS_OR_KEY);
        BOOST_CHECK_EQUAL(e.message, "No such mempool or blockchain transaction");
    }
    RPCClient wrong("127.0.0.1", fake.port, "user", "nope", 10);
    BOOST_CHECK_THROW(wrong.Call("getblockcount"), NodeUnauthorized);
}

BOOST_AUTO_TEST_CASE(chain_source)
{
    FakeLitecoind fake;
    RPCClient node("127.0.0.1", fake.port, "user", "pass", 10);
    RPCChainSource source(node);
    BOOST_CHECK_EQUAL(source.GetBlockHash(5), "getblockhash/1");
    BOOST_CHECK_THROW(source.GetBlockHash(6), HeightOutOfRange);
    BlockHeader h = source.GetBlockHeader("abc");
    BOOST_CHECK_EQUAL(h.hash, "abc");
    BOOST_CHECK_EQUAL(h.height, 5);
    BOOST_CHECK_EQUAL(h.prevHash, "parent");
    BOOST_CHECK_EQUAL(h.time, 1700000000);
    RPCClient wrong("127.0.0.1", fake.port, "user", "nope", 10);
    RPCChainSource wrongSource(wrong);
    BOOST_CHECK_THROW(wrongSource.GetBlockCount(), NodeUnauthorized); // not a retryable NodeError
}

BOOST_AUTO_TEST_CASE(unreachable)
{
    int port;
    {
        FakeLitecoind fake;
        port = fake.port;
    }
    RPCClient node("127.0.0.1", port, "user", "pass", 5);
    BOOST_CHECK_THROW(node.Call("getblockcount"), NodeUnreachable);
}

BOOST_AUTO_TEST_SUITE_END()
