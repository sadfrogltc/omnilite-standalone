// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Runs blocks through OmniLite's handlers via the indexer, including a reorg
// and restarts, on regtest.

#include <indexer.h>

#include <chainparams.h>
#include <hash.h>
#include <httprpc.h>
#include <mempool.h>
#include <txmempool.h>
#include <key_io.h>
#include <omnicore/activation.h>
#include <omnicore/createpayload.h>
#include <omnicore/dex.h>
#include <omnicore/notifications.h>
#include <omnicore/pending.h>
#include <omnicore/sp.h>
#include <omnicore/encoding.h>
#include <omnicore/omnicore.h>
#include <omnicore/rules.h>
#include <omnicore/tally.h>
#include <rpc/server.h>
#include <rpcclient.h>
#include <store.h>
#include <test/omnicore/test_shim.h>
#include <test/util.h>

#include <boost/test/unit_test.hpp>

/** Empties OmniLite's in-memory state, as a fresh process finds it (the memory part of clear_all_state). */
void ClearMemoryState()
{
    mastercore::mp_tally_map.clear();
    mastercore::my_offers.clear();
    mastercore::my_accepts.clear();
    mastercore::my_crowds.clear();
    mastercore::my_pending.clear();
    mastercore::ResetConsensusParams();
    mastercore::ClearActivations();
    mastercore::ClearAlerts();
    mastercore::ClearFreezeState();
    mastercore::view.Flush();
}

namespace {

CScript ScriptFor(unsigned char b)
{
    uint160 h;
    for (unsigned char& c : h) c = b;
    return GetScriptForDestination(CKeyID(h));
}

std::string AddressOf(const CScript& script)
{
    CTxDestination dest;
    BOOST_REQUIRE(ExtractDestination(script, dest));
    return EncodeDestination(dest);
}

std::string Hash(const std::string& name)
{
    CHashWriter hw(SER_GETHASH, 0);
    hw << name;
    return hw.GetHash().GetHex();
}

/** A simulated node: blocks by hash and the transactions it knows. */
class FakeNode : public NodeData
{
public:
    std::map<std::string, std::vector<CTransactionRef>> blocks;
    std::map<uint256, CTransactionRef> txs;
    /** The block of each transaction in one; unknown means unconfirmed. */
    std::map<uint256, uint256> txBlock;
    std::vector<uint256> mempoolTxs;
    bool unreachable = false;

    std::vector<uint256> GetMempool() override
    {
        if (unreachable) throw NodeError("connection refused");
        return mempoolTxs;
    }

    /** tx enters the mempool (unconfirmed). */
    void Broadcast(const CTransactionRef& tx)
    {
        txs[tx->GetHash()] = tx;
        mempoolTxs.push_back(tx->GetHash());
    }

    std::vector<CTransactionRef> GetBlockTxs(const std::string& hash) override
    {
        if (unreachable) throw NodeError("connection refused");
        auto it = blocks.find(hash);
        if (it == blocks.end()) throw NodeError("unknown block " + hash);
        return it->second;
    }

    bool GetTx(const uint256& txid, CTransactionRef& tx, uint256& blockHash) override
    {
        if (unreachable) throw NodeError("connection refused");
        auto it = txs.find(txid);
        if (it == txs.end()) return false;
        tx = it->second;
        auto bit = txBlock.find(txid);
        blockHash = bit == txBlock.end() ? uint256() : bit->second;
        return true;
    }

    /** An output of value to script, known to the node. */
    COutPoint Fund(const CScript& script, int64_t value)
    {
        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vin[0].prevout = COutPoint(uint256S(strprintf("%064x", ++m_salt)), 0);
        mtx.vout.push_back(CTxOut(value, script));
        CTransactionRef tx = MakeTransactionRef(std::move(mtx));
        txs[tx->GetHash()] = tx;
        return COutPoint(tx->GetHash(), 0);
    }

    /** A Class C Omni transaction from script, with an optional reference output. */
    CTransactionRef OmniTx(const CScript& from, const std::vector<unsigned char>& payload, const CScript* to = nullptr)
    {
        CMutableTransaction mtx;
        mtx.vin.push_back(CTxIn(Fund(from, 100000)));
        std::vector<std::pair<CScript, int64_t>> outputs;
        BOOST_REQUIRE(OmniCore_Encode_ClassC(payload, outputs));
        for (const auto& o : outputs) mtx.vout.push_back(CTxOut(o.second, o.first));
        if (to) mtx.vout.push_back(CTxOut(5460, *to));
        return MakeTransactionRef(std::move(mtx));
    }

private:
    int m_salt = 0;
};

struct Fixture : BasicTestingSetup {
    TempDir dir;
    FakeNode node;
    std::unique_ptr<CStore> store;
    std::unique_ptr<OmniIndexer> indexer;
    const CScript alice = ScriptFor(0xa1), bob = ScriptFor(0xb2), carol = ScriptFor(0xc3);
    int64_t start;

    Fixture()
    {
        SelectParams(CBaseChainParams::REGTEST);
        start = mastercore::ConsensusParams().GENESIS_BLOCK;
        Open();
    }
    ~Fixture() { Close(); }

    void Open()
    {
        store = std::make_unique<CStore>(dir.Sub("index"), "regtest", 100);
        indexer = std::make_unique<OmniIndexer>(node, *store);
        indexer->Start(dir.Sub("omnicore"), OmniOptions());
    }

    /** Ends the "process": OmniLite's in-memory state is gone afterwards. */
    void Close()
    {
        indexer.reset();
        store.reset();
        ClearMemoryState();
    }

    BlockHeader Header(const std::string& name, int64_t height)
    {
        std::optional<BlockInfo> tip = store->Tip();
        return {Hash(name), height, tip ? tip->hash : Hash("genesis"), 1700000000 + height * 600};
    }

    void Connect(const std::string& name, int64_t height, const std::vector<CTransactionRef>& txs)
    {
        BlockHeader h = Header(name, height);
        node.blocks[h.hash] = txs;
        for (const auto& tx : txs) {
            node.txs[tx->GetHash()] = tx;
            node.txBlock[tx->GetHash()] = uint256S(h.hash);
        }
        std::unique_ptr<CBlockTx> btx = store->Begin({h.height, h.hash, h.prevHash, h.time});
        indexer->ConnectBlock(*btx, h);
        btx->Commit();
    }

    void Disconnect()
    {
        BlockInfo b = store->Disconnect();
        indexer->DisconnectBlock(b);
    }

    int64_t Balance(const CScript& who, uint32_t property)
    {
        return GetTokenBalance(AddressOf(who), property, BALANCE);
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(indexer_tests, Fixture)

BOOST_AUTO_TEST_CASE(issue_send_reorg_restart)
{
    // Block 1: alice issues 1000 divisible tokens (property 3).
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN);
    Connect("b1", start, {node.OmniTx(alice, issue)});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 1000 * COIN);

    // Block 2: 400 to bob.
    Connect("b2", start + 1, {node.OmniTx(alice, CreatePayload_SimpleSend(3, 400 * COIN), &bob)});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 600 * COIN);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 400 * COIN);

    // Reorg: block 2 is replaced by one sending 100 to carol. OmniLite loads
    // the state saved after block 1 and goes on from there.
    Disconnect();
    Connect("b2'", start + 1, {node.OmniTx(alice, CreatePayload_SimpleSend(3, 100 * COIN), &carol)});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 900 * COIN);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 0);
    BOOST_CHECK_EQUAL(Balance(carol, 3), 100 * COIN);

    // A send of more than alice has is invalid and moves nothing.
    Connect("b3", start + 2, {node.OmniTx(alice, CreatePayload_SimpleSend(3, 5000 * COIN), &bob)});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 900 * COIN);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 0);

    // Restart: a fresh process loads the saved state of the tip.
    Close();
    BOOST_CHECK(mastercore::mp_tally_map.empty());
    Open();
    BOOST_CHECK_EQUAL(Balance(alice, 3), 900 * COIN);
    BOOST_CHECK_EQUAL(Balance(carol, 3), 100 * COIN);
}

BOOST_AUTO_TEST_CASE(restart_replays_missing_blocks)
{
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_INDIVISIBLE, 0, "Games", "", "Coin", "", "", 50);
    Connect("c1", start, {node.OmniTx(alice, issue)});
    Connect("c2", start + 1, {node.OmniTx(alice, CreatePayload_SimpleSend(3, 20), &bob)});

    // Lose the saved state of the tip: on restart OmniLite loads block 1's
    // state and replays block 2, which it reads from the node.
    Close();
    for (const auto& f : fs::directory_iterator(dir.Sub("omnicore") + "/MP_persist")) {
        if (f.path().filename().string().find(Hash("c2")) != std::string::npos) fs::remove(f.path());
    }
    Open();
    BOOST_CHECK_EQUAL(Balance(alice, 3), 30);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 20);
}

BOOST_AUTO_TEST_CASE(node_failure_before_processing_is_retryable)
{
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_INDIVISIBLE, 0, "Games", "", "Coin", "", "", 50);
    Connect("e1", start, {node.OmniTx(alice, issue)});

    // The node fails while the block and its inputs are fetched: nothing is
    // applied yet, so the syncer may retry (NodeError, not FatalError).
    CTransactionRef send = node.OmniTx(alice, CreatePayload_SimpleSend(3, 20), &bob);
    BlockHeader h = Header("e2", start + 1);
    node.blocks[h.hash] = {send};
    node.unreachable = true;
    {
        std::unique_ptr<CBlockTx> btx = store->Begin({h.height, h.hash, h.prevHash, h.time});
        BOOST_CHECK_THROW(indexer->ConnectBlock(*btx, h), NodeError);
    }
    BOOST_CHECK_EQUAL(Balance(bob, 3), 0);

    // Retried once the node answers again.
    node.unreachable = false;
    Connect("e2", start + 1, {send});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 20);
}

BOOST_AUTO_TEST_CASE(api_answers_from_the_state)
{
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN);
    Connect("a1", start, {node.OmniTx(alice, issue)});
    CTransactionRef send = node.OmniTx(alice, CreatePayload_SimpleSend(3, 400 * COIN), &bob);
    Connect("a2", start + 1, {send});

    RegisterOmniRPCCommands();
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    APIOptions options;
    options.user = "u";
    options.password = "p";
    APIServer api(options, nullptr);
    api.Start();
    RPCClient client("127.0.0.1", api.Port(), "u", "p", 10);
    auto call = [&](const std::string& method, std::vector<UniValue> args) {
        UniValue params(UniValue::VARR);
        for (const UniValue& a : args) params.push_back(a);
        return client.Call(method, params);
    };

    UniValue info = call("omni_getinfo", {});
    BOOST_CHECK_EQUAL(find_value(info, "block").get_int64(), start + 1);

    UniValue property = call("omni_getproperty", {UniValue(3)});
    BOOST_CHECK_EQUAL(find_value(property, "name").get_str(), "Coin");
    BOOST_CHECK_EQUAL(find_value(property, "issuer").get_str(), AddressOf(alice));
    BOOST_CHECK(find_value(property, "divisible").isTrue());
    BOOST_CHECK_EQUAL(find_value(property, "totaltokens").get_str(), "1000.00000000");

    UniValue balance = call("omni_getbalance", {UniValue(AddressOf(bob)), UniValue(3)});
    BOOST_CHECK_EQUAL(find_value(balance, "balance").get_str(), "400.00000000");

    // The transaction comes from the node, its validity from OmniLite.
    UniValue tx = call("omni_gettransaction", {UniValue(send->GetHash().GetHex())});
    BOOST_CHECK(find_value(tx, "valid").isTrue());
    BOOST_CHECK_EQUAL(find_value(tx, "block").get_int64(), start + 1);
    BOOST_CHECK_EQUAL(find_value(tx, "confirmations").get_int(), 1);
    BOOST_CHECK_EQUAL(find_value(tx, "sendingaddress").get_str(), AddressOf(alice));
    BOOST_CHECK_EQUAL(find_value(tx, "referenceaddress").get_str(), AddressOf(bob));
    BOOST_CHECK_EQUAL(find_value(tx, "amount").get_str(), "400.00000000");

    UniValue blockTxs = call("omni_listblocktransactions", {UniValue(start + 1)});
    BOOST_REQUIRE_EQUAL(blockTxs.size(), 1u);
    BOOST_CHECK_EQUAL(blockTxs[0].get_str(), send->GetHash().GetHex());

    // Unknown transactions get omnilitecored's error.
    BOOST_CHECK_EXCEPTION(call("omni_gettransaction", {UniValue(uint256S("01").GetHex())}), RPCError,
        [](const RPCError& e) { return e.code == RPC_INVALID_ADDRESS_OR_KEY; });

    // Payloads use the property's divisibility; named arguments work.
    UniValue named(UniValue::VOBJ);
    named.pushKV("propertyid", 3);
    named.pushKV("amount", "0.5");
    BOOST_CHECK_EQUAL(client.Call("omni_createpayload_simplesend", named).get_str(), "00000000000000030000000002faf080");
}

BOOST_AUTO_TEST_CASE(pending_transactions_from_the_mempool)
{
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN);
    Connect("m1", start, {node.OmniTx(alice, issue)});

    // The node's mempool: an Omni send and a plain payment.
    CTransactionRef send = node.OmniTx(alice, CreatePayload_SimpleSend(3, 400 * COIN), &bob);
    node.Broadcast(send);
    COutPoint plain = node.Fund(carol, 1000);
    node.mempoolTxs.push_back(plain.hash);

    MempoolWatcher watcher(node);
    watcher.Refresh();
    BOOST_CHECK(IsInMarkerCache(send->GetHash()));
    BOOST_CHECK(!IsInMarkerCache(plain.hash));
    BOOST_CHECK(mempool.exists(send->GetHash()));
    BOOST_CHECK(mempool.exists(plain.hash));

    RegisterOmniRPCCommands();
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    APIOptions options;
    options.user = "u";
    options.password = "p";
    APIServer api(options, nullptr);
    api.Start();
    RPCClient client("127.0.0.1", api.Port(), "u", "p", 10);
    auto pending = [&](const std::string& filter) {
        UniValue params(UniValue::VARR);
        if (!filter.empty()) params.push_back(filter);
        return client.Call("omni_listpendingtransactions", params);
    };

    // Listed unconfirmed, without a verdict on validity; balances are untouched.
    UniValue list = pending("");
    BOOST_REQUIRE_EQUAL(list.size(), 1u);
    BOOST_CHECK_EQUAL(find_value(list[0], "txid").get_str(), send->GetHash().GetHex());
    BOOST_CHECK_EQUAL(find_value(list[0], "amount").get_str(), "400.00000000");
    BOOST_CHECK_EQUAL(find_value(list[0], "confirmations").get_int(), 0);
    BOOST_CHECK(find_value(list[0], "valid").isNull());
    BOOST_CHECK_EQUAL(pending(AddressOf(bob)).size(), 1u);
    BOOST_CHECK_EQUAL(pending(AddressOf(carol)).size(), 0u);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 0);
    UniValue params(UniValue::VARR);
    params.push_back(send->GetHash().GetHex());
    BOOST_CHECK_EQUAL(find_value(client.Call("omni_gettransaction", params), "confirmations").get_int(), 0);

    // Mined: out of the mempool and the marker cache at once, as in Core.
    node.mempoolTxs = {plain.hash};
    Connect("m2", start + 1, {send});
    BOOST_CHECK(!IsInMarkerCache(send->GetHash()));
    BOOST_CHECK(!mempool.exists(send->GetHash()));
    BOOST_CHECK_EQUAL(pending("").size(), 0u);
    BOOST_CHECK_EQUAL(Balance(bob, 3), 400 * COIN);

    // A pending transaction the node drops (e.g. replaced) disappears with the next refresh.
    CTransactionRef dropped = node.OmniTx(alice, CreatePayload_SimpleSend(3, 1 * COIN), &carol);
    node.Broadcast(dropped);
    watcher.Refresh();
    BOOST_CHECK_EQUAL(pending("").size(), 1u);
    node.mempoolTxs.clear();
    watcher.Refresh();
    BOOST_CHECK(!IsInMarkerCache(dropped->GetHash()));
    BOOST_CHECK_EQUAL(pending("").size(), 0u);

    // A failing node leaves the last known mempool in place.
    node.Broadcast(dropped);
    watcher.Refresh();
    node.unreachable = true;
    BOOST_CHECK_THROW(watcher.Refresh(), NodeError);
    node.unreachable = false;
    BOOST_CHECK(mempool.exists(dropped->GetHash()));

    node.mempoolTxs.clear();
    watcher.Refresh();
}

BOOST_AUTO_TEST_SUITE_END()
