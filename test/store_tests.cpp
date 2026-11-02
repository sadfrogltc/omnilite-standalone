// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <store.h>

#include <test/util.h>

#include <boost/test/unit_test.hpp>

#include <functional>

namespace {

std::string Hash(int64_t h) { return "h" + std::to_string(h); }

void Connect(CStore& s, int64_t h, const std::function<void(CBlockTx&)>& fn = {})
{
    std::unique_ptr<CBlockTx> tx = s.Begin({h, Hash(h), Hash(h - 1), 1700000000 + h});
    if (fn) fn(*tx);
    tx->Commit();
}

/** The value of key in namespace "test", or "<none>". */
std::string Value(const KVReader& r, const std::string& key)
{
    std::string v;
    return r.Get("test", key, v) ? v : "<none>";
}

} // namespace

BOOST_AUTO_TEST_SUITE(store_tests)

BOOST_AUTO_TEST_CASE(disconnect_restores_state)
{
    TempDir dir;
    CStore s(dir.Sub("db"), "regtest", 100);

    Connect(s, 100, [](CBlockTx& tx) {
        tx.Put("test", "a", "1");
        tx.Put("test", "empty", "");
    });
    Connect(s, 101, [](CBlockTx& tx) {
        tx.Put("test", "a", "2");
        tx.Put("test", "a", "3");
        tx.Put("test", "b", "x");
        tx.Delete("test", "empty");
        BOOST_CHECK_EQUAL(Value(tx, "a"), "3"); // the block sees its own changes
    });
    BOOST_CHECK_EQUAL(Value(s, "a"), "3");
    BOOST_CHECK_EQUAL(Value(s, "b"), "x");
    BOOST_CHECK_EQUAL(Value(s, "empty"), "<none>");
    BOOST_CHECK_EQUAL(s.Tip()->height, 101);
    BOOST_CHECK_EQUAL(s.BlockAt(100)->hash, "h100");
    BOOST_CHECK_EQUAL(s.BlockAt(101)->time, 1700000101);

    BlockInfo b = s.Disconnect();
    BOOST_CHECK_EQUAL(b.height, 101);
    BOOST_CHECK_EQUAL(Value(s, "a"), "1");
    BOOST_CHECK_EQUAL(Value(s, "b"), "<none>");
    BOOST_CHECK_EQUAL(Value(s, "empty"), "");
    BOOST_CHECK(!s.BlockAt(101));

    s.Disconnect();
    BOOST_CHECK_EQUAL(Value(s, "a"), "<none>");
    BOOST_CHECK_EQUAL(Value(s, "empty"), "<none>");
    BOOST_CHECK(!s.Tip());
    BOOST_CHECK_THROW(s.Disconnect(), StoreError);
}

BOOST_AUTO_TEST_CASE(begin_rejects_block_not_on_tip)
{
    TempDir dir;
    CStore s(dir.Sub("db"), "regtest", 100);
    Connect(s, 10);
    BOOST_CHECK_THROW(s.Begin({12, "x", Hash(10), 0}), NotOnTipError); // gap
    BOOST_CHECK_THROW(s.Begin({11, "x", "other", 0}), NotOnTipError);  // wrong parent
}

BOOST_AUTO_TEST_CASE(uncommitted_block_leaves_no_trace)
{
    TempDir dir;
    CStore s(dir.Sub("db"), "regtest", 100);
    Connect(s, 1);
    {
        std::unique_ptr<CBlockTx> tx = s.Begin({2, Hash(2), Hash(1), 0});
        tx->Put("test", "a", "1");
    } // dropped without Commit, e.g. after a processing error
    BOOST_CHECK_EQUAL(s.Tip()->height, 1);
    BOOST_CHECK_EQUAL(Value(s, "a"), "<none>");
}

BOOST_AUTO_TEST_CASE(undo_pruning)
{
    TempDir dir;
    CStore s(dir.Sub("db"), "regtest", 10);
    for (int64_t h = 1; h <= 30; ++h) {
        Connect(s, h, [h](CBlockTx& tx) { tx.Put("test", "a", std::to_string(h)); });
    }
    // Blocks 21..30 keep their undo data.
    for (int i = 0; i < 10; ++i) s.Disconnect();
    BOOST_CHECK_EQUAL(Value(s, "a"), "20");
    BOOST_CHECK_THROW(s.Disconnect(), UndoUnavailableError);
}

BOOST_AUTO_TEST_CASE(reopen_and_network_mismatch)
{
    TempDir dir;
    {
        CStore s(dir.Sub("db"), "regtest", 100);
        Connect(s, 5, [](CBlockTx& tx) { tx.Put("test", "a", "kept"); });
    }
    {
        CStore s(dir.Sub("db"), "regtest", 100);
        BOOST_CHECK_EQUAL(s.Tip()->hash, "h5");
        BOOST_CHECK_EQUAL(Value(s, "a"), "kept");
    }
    BOOST_CHECK_THROW(CStore(dir.Sub("db"), "main", 100), StoreError);
}

BOOST_AUTO_TEST_CASE(scan)
{
    TempDir dir;
    CStore s(dir.Sub("db"), "regtest", 100);
    Connect(s, 1, [](CBlockTx& tx) {
        for (const char* k : {"a/2", "a/1", "ab", "b/1", "a/3"}) tx.Put("test", k, k);
        tx.Put("other", "a/9", "x"); // other namespaces stay apart
    });
    Connect(s, 2, [](CBlockTx& tx) {
        tx.Delete("test", "a/3");
        tx.Put("test", "a/0", "new");
        KeyValues kvs = tx.Scan("test", "a/");
        BOOST_REQUIRE_EQUAL(kvs.size(), 3U);
        BOOST_CHECK_EQUAL(kvs[0].first, "a/0");
        BOOST_CHECK_EQUAL(kvs[1].first, "a/1");
        BOOST_CHECK_EQUAL(kvs[2].first, "a/2");
    });
    BOOST_CHECK_EQUAL(s.Scan("test", "").size(), 5U);
    BOOST_CHECK_EQUAL(s.Scan("other", "").size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
