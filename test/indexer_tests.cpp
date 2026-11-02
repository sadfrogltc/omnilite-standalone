// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <indexer.h>

#include <chainparams.h>
#include <key_io.h>
#include <omnicore/createpayload.h>
#include <omnicore/encoding.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/rules.h>
#include <streams.h>
#include <test/omnicore/test_shim.h>
#include <test/util.h>
#include <util/strencodings.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

namespace {

CScript ScriptFor(unsigned char b, bool p2sh = false)
{
    uint160 h;
    for (unsigned char& c : h) c = b;
    return p2sh ? GetScriptForDestination(CScriptID(h)) : GetScriptForDestination(CKeyID(h));
}

std::string AddressOf(const CScript& script)
{
    CTxDestination dest;
    BOOST_REQUIRE(ExtractDestination(script, dest));
    return EncodeDestination(dest);
}

/** A transaction spending one fresh output of value from script, with the given outputs. */
CTransactionRef Spend(TestCoinsView& view, const CScript& from, int64_t value, const std::vector<CTxOut>& outs, int salt)
{
    CMutableTransaction prev;
    prev.vin.resize(1);
    prev.vin[0].prevout = COutPoint(uint256S(strprintf("%064x", 1000 + salt)), 0);
    prev.vout.push_back(CTxOut(value, from));
    CTransaction prevTx(prev);
    view.AddCoin(COutPoint(prevTx.GetHash(), 0), Coin{prevTx.vout[0]}, true);

    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn(prevTx.GetHash(), 0));
    mtx.vout = outs;
    return MakeTransactionRef(std::move(mtx));
}

std::vector<CTxOut> ClassC(const std::vector<unsigned char>& payload)
{
    std::vector<std::pair<CScript, int64_t>> outputs;
    BOOST_REQUIRE(OmniCore_Encode_ClassC(payload, outputs));
    std::vector<CTxOut> outs;
    for (const auto& o : outputs) outs.push_back(CTxOut(o.second, o.first));
    return outs;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(indexer_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(records_valid_omni_transactions)
{
    TempDir dir;
    CStore store(dir.Sub("db"), "main", 100);
    TestCoinsView view;
    const int64_t height = mastercore::ConsensusParams().GENESIS_BLOCK + 10;
    const CScript alice = ScriptFor(0xa1, true), bob = ScriptFor(0xb2);

    std::vector<CTransactionRef> txs;
    // 0: a coinbase
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vout.push_back(CTxOut(1250000000, alice));
    txs.push_back(MakeTransactionRef(std::move(coinbase)));
    // 1: a plain payment
    txs.push_back(Spend(view, alice, 100000, {CTxOut(90000, bob)}, 1));
    // 2: a Class C simple send from alice to bob, change first
    std::vector<CTxOut> send = ClassC(CreatePayload_SimpleSend(3, 500));
    send.insert(send.begin(), CTxOut(40000, alice));
    send.push_back(CTxOut(5460, bob));
    txs.push_back(Spend(view, alice, 100000, send, 2));
    // 3: a structurally invalid payload (type 2, restricted send, has no interpreter)
    std::vector<unsigned char> restricted = CreatePayload_SimpleSend(3, 500);
    restricted[3] = 2;
    txs.push_back(Spend(view, alice, 100000, ClassC(restricted), 3));
    // 4: Class A without data: a possible DEx payment, recorded only once it buys tokens
    txs.push_back(Spend(view, bob, 100000, {CTxOut(5460, GetScriptForDestination(ExodusAddress())), CTxOut(50000, alice)}, 4));

    std::unique_ptr<CBlockTx> btx = store.Begin({height, "b10", "", 1700000000});
    ProcessBlockTxs(*btx, height, 1700000000, txs, view);
    btx->Commit();

    BOOST_CHECK_EQUAL(CountOmniTxs(store), 1);
    std::optional<OmniTxRecord> rec = GetOmniTx(store, txs[2]->GetHash().GetHex());
    BOOST_REQUIRE(rec);
    BOOST_CHECK_EQUAL(rec->block, height);
    BOOST_CHECK_EQUAL(rec->index, 2U);
    BOOST_CHECK_EQUAL(rec->encodingClass, OMNI_CLASS_C);
    BOOST_CHECK_EQUAL(rec->sender, AddressOf(alice));
    BOOST_CHECK_EQUAL(rec->reference, AddressOf(bob));
    BOOST_CHECK_EQUAL(rec->fee, 100000 - 40000 - 5460);
    BOOST_CHECK_EQUAL(rec->type, MSC_TYPE_SIMPLE_SEND);
    BOOST_CHECK_EQUAL(rec->payload, HexStr(CreatePayload_SimpleSend(3, 500)));
    std::string txid;
    BOOST_CHECK(store.Get(NS_BLOCKTX, BlockTxKey(height, 2), txid));
    BOOST_CHECK_EQUAL(txid, rec->txid);
    for (int i : {0, 1, 3, 4}) BOOST_CHECK(!GetOmniTx(store, txs[i]->GetHash().GetHex()));

    // The next block adds to the count; a reorg takes it back.
    std::unique_ptr<CBlockTx> next = store.Begin({height + 1, "b11", "b10", 1700000600});
    std::vector<CTxOut> again = ClassC(CreatePayload_SimpleSend(3, 1));
    again.push_back(CTxOut(5460, bob));
    ProcessBlockTxs(*next, height + 1, 1700000600, {Spend(view, alice, 100000, again, 5)}, view);
    next->Commit();
    BOOST_CHECK_EQUAL(CountOmniTxs(store), 2);
    store.Disconnect();
    BOOST_CHECK_EQUAL(CountOmniTxs(store), 1);
    BOOST_CHECK(store.Scan(NS_BLOCKTX, "").size() == 1);
}

BOOST_AUTO_TEST_CASE(before_genesis_class_c_is_ignored)
{
    TempDir dir;
    CStore store(dir.Sub("db"), "main", 100);
    TestCoinsView view;
    const int64_t height = mastercore::ConsensusParams().GENESIS_BLOCK - 1;
    std::vector<CTxOut> send = ClassC(CreatePayload_SimpleSend(3, 500));
    send.push_back(CTxOut(5460, ScriptFor(0xb2)));
    std::unique_ptr<CBlockTx> btx = store.Begin({height, "a", "", 0});
    ProcessBlockTxs(*btx, height, 0, {Spend(view, ScriptFor(0xa1), 100000, send, 1)}, view);
    btx->Commit();
    BOOST_CHECK_EQUAL(CountOmniTxs(store), 0);
}

BOOST_AUTO_TEST_CASE(decode_tx)
{
    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn(uint256S("01"), 3));
    mtx.vout.push_back(CTxOut(1234, ScriptFor(0x11)));
    CTransaction tx(mtx);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;
    std::string hex = HexStr(ss.begin(), ss.end());
    BOOST_CHECK(DecodeTx(hex)->GetHash() == tx.GetHash());
    BOOST_CHECK_THROW(DecodeTx("zz"), std::exception);
    BOOST_CHECK_THROW(DecodeTx(hex + "00"), std::exception);
    // An MWEB flag (0x08) is unknown to Litecoin Core 0.18.
    std::string mweb = hex.substr(0, 8) + "0008" + hex.substr(8);
    BOOST_CHECK_THROW(DecodeTx(mweb), std::exception);
}

BOOST_AUTO_TEST_SUITE_END()
