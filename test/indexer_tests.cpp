// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Runs blocks through OmniLite's handlers via the indexer, including a reorg
// and restarts, on regtest.

#include <indexer.h>

#include <chainparams.h>
#include <hash.h>
#include <core_io.h>
#include <httprpc.h>
#include <mempool.h>
#include <omnicore/errors.h>
#include <omnicore/script.h>
#include <omnicore/utilsbitcoin.h>
#include <txmempool.h>
#include <version.h>
#include <wallet/wallet.h>
// After rpc/protocol.h (via wallet/wallet.h): libevent defines HTTP_OK and others as macros.
#include <test/fakelitecoind.h>

#include <algorithm>
#include <mutex>
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

/**
 * The wallet part of a litecoind: outputs of the wallet's addresses, a
 * signature of fixed size for every input, broadcasting with bookkeeping.
 */
struct FakeWallet {
    std::mutex mutex;
    std::vector<NodeCoin> coins;
    std::set<std::string> mine;
    std::vector<CTransactionRef> sent;
    std::vector<std::string> uris;
    int signs = 0;
    bool locked = false;

    static UniValue Error(int code, const std::string& message)
    {
        UniValue e(UniValue::VOBJ);
        e.pushKV("code", code);
        e.pushKV("message", message);
        return e;
    }

    bool Handle(const std::string& method, const UniValue& params, const std::string& uri, UniValue& result, UniValue& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        uris.push_back(uri);
        if (method == "listunspent") {
            result = UniValue(UniValue::VARR);
            const std::string address = params[2][0].get_str();
            for (const NodeCoin& c : coins) {
                if (c.address != address) continue;
                UniValue u(UniValue::VOBJ);
                u.pushKV("txid", c.outpoint.hash.GetHex());
                u.pushKV("vout", static_cast<int>(c.outpoint.n));
                u.pushKV("address", c.address);
                u.pushKV("scriptPubKey", HexStr(c.out.scriptPubKey.begin(), c.out.scriptPubKey.end()));
                u.pushKV("amount", ValueFromAmount(c.out.nValue));
                u.pushKV("confirmations", 6);
                u.pushKV("spendable", true);
                u.pushKV("safe", true);
                result.push_back(u);
            }
        } else if (method == "getwalletinfo") {
            result = UniValue(UniValue::VOBJ);
            result.pushKV("paytxfee", ValueFromAmount(0));
        } else if (method == "estimatesmartfee") {
            result = UniValue(UniValue::VOBJ);
            result.pushKV("feerate", ValueFromAmount(20000)); // 0.0002 LTC/kB
        } else if (method == "getnetworkinfo") {
            result = UniValue(UniValue::VOBJ);
            result.pushKV("relayfee", ValueFromAmount(1000));
        } else if (method == "getmempoolinfo") {
            result = UniValue(UniValue::VOBJ);
            result.pushKV("mempoolminfee", ValueFromAmount(1000));
        } else if (method == "getaddressinfo") {
            result = UniValue(UniValue::VOBJ);
            result.pushKV("ismine", mine.count(params[0].get_str()) > 0);
            result.pushKV("pubkey", "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
        } else if (method == "signrawtransactionwithwallet") {
            if (locked) {
                error = Error(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
                return true;
            }
            CMutableTransaction mtx;
            BOOST_REQUIRE(DecodeHexTx(mtx, params[0].get_str()));
            for (CTxIn& in : mtx.vin) in.scriptSig = CScript() << std::vector<unsigned char>(72, 0x30) << std::vector<unsigned char>(33, 0x02);
            ++signs;
            result = UniValue(UniValue::VOBJ);
            result.pushKV("hex", EncodeHexTx(CTransaction(mtx)));
            result.pushKV("complete", true);
        } else if (method == "sendrawtransaction") {
            CMutableTransaction mtx;
            BOOST_REQUIRE(DecodeHexTx(mtx, params[0].get_str()));
            CTransactionRef tx = MakeTransactionRef(mtx);
            // Spent outputs leave the wallet, outputs to its addresses come in.
            for (const CTxIn& in : tx->vin) {
                coins.erase(std::remove_if(coins.begin(), coins.end(), [&](const NodeCoin& c) { return c.outpoint == in.prevout; }), coins.end());
            }
            for (unsigned int i = 0; i < tx->vout.size(); ++i) {
                CTxDestination dest;
                if (ExtractDestination(tx->vout[i].scriptPubKey, dest) && mine.count(EncodeDestination(dest))) {
                    coins.push_back({COutPoint(tx->GetHash(), i), tx->vout[i], EncodeDestination(dest)});
                }
            }
            sent.push_back(tx);
            result = tx->GetHash().GetHex();
        } else {
            return false;
        }
        return true;
    }
};

int64_t SignedSize(const CTransaction& tx)
{
    return ::GetSerializeSize(tx, PROTOCOL_VERSION); // no witness data here
}

BOOST_AUTO_TEST_CASE(send_through_the_node_wallet)
{
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN);
    Connect("s1", start, {node.OmniTx(alice, issue)});

    // alice's address holds 1 LTC in the node's wallet.
    FakeWallet wallet;
    const COutPoint funding = node.Fund(alice, 1 * COIN);
    wallet.coins.push_back({funding, node.txs[funding.hash]->vout[0], AddressOf(alice)});
    wallet.mine.insert(AddressOf(alice));
    FakeLitecoind litecoind;
    litecoind.handler = [&](const std::string& m, const UniValue& p, const std::string& u, UniValue& r, UniValue& e) { return wallet.Handle(m, p, u, r, e); };
    RPCClient nodeRPC("127.0.0.1", litecoind.port, "user", "pass", 10);

    RegisterOmniRPCCommands();
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    SetWalletNode(&nodeRPC);
    APIOptions options;
    options.user = "u";
    options.password = "p";
    APIServer api(options, &nodeRPC);
    api.Start();
    RPCClient client("127.0.0.1", api.Port(), "u", "p", 10);
    auto send = [&](const RPCClient& c, const std::string& to, const std::string& amount) {
        UniValue params(UniValue::VARR);
        params.push_back(AddressOf(alice));
        params.push_back(to);
        params.push_back(3);
        params.push_back(amount);
        return c.Call("omni_send", params);
    };

    // omni_send: alice's coin in, payload, change back to alice, bob as reference last.
    const std::string txid = send(client, AddressOf(bob), "100").get_str();
    BOOST_REQUIRE_EQUAL(wallet.sent.size(), 1u);
    CTransactionRef tx = wallet.sent[0];
    BOOST_CHECK_EQUAL(tx->GetHash().GetHex(), txid);
    BOOST_REQUIRE_EQUAL(tx->vin.size(), 1u);
    BOOST_CHECK(tx->vin[0].prevout == funding);
    BOOST_REQUIRE_EQUAL(tx->vout.size(), 3u);
    BOOST_CHECK(tx->vout[0].scriptPubKey[0] == OP_RETURN);
    BOOST_CHECK(tx->vout[1].scriptPubKey == alice);
    BOOST_CHECK(tx->vout[2].scriptPubKey == bob);
    BOOST_CHECK_EQUAL(tx->vout[2].nValue, OmniGetDustThreshold(bob));
    // The fee is the node's rate (estimatesmartfee, 0.0002 LTC/kB) for the signed size.
    const CAmount fee = 1 * COIN - tx->GetValueOut();
    BOOST_CHECK_EQUAL(fee, CFeeRate(20000).GetFee(SignedSize(*tx)));
    // Pending at once, as after AcceptToMemoryPool.
    BOOST_CHECK(IsInMarkerCache(tx->GetHash()));
    BOOST_CHECK(mempool.exists(tx->GetHash()));

    // OmniLite reads the transaction like any other: 100 tokens to bob.
    Connect("s2", start + 1, {tx});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 100 * COIN);
    BOOST_CHECK_EQUAL(Balance(alice, 3), 900 * COIN);

    // Dry run: omni_setautocommit false returns the signed transaction, nothing is sent.
    auto setAutoCommit = [&](bool on) {
        UniValue value;
        value.setBool(on);
        UniValue flag(UniValue::VARR);
        flag.push_back(value);
        client.Call("omni_setautocommit", flag);
    };
    BOOST_TEST_CHECKPOINT("dry run");
    setAutoCommit(false);
    const std::string hex = send(client, AddressOf(carol), "1").get_str();
    setAutoCommit(true);
    CMutableTransaction dry;
    BOOST_REQUIRE(DecodeHexTx(dry, hex));
    BOOST_CHECK_EQUAL(wallet.sent.size(), 1u);
    BOOST_CHECK(dry.vin[0].prevout == COutPoint(tx->GetHash(), 1)); // the change of the first send

    // A wallet named in the URI (litecoin-cli -rpcwallet) is the one asked.
    wallet.uris.clear();
    send(client.ForWallet("w1"), AddressOf(carol), "1");
    BOOST_CHECK(std::count(wallet.uris.begin(), wallet.uris.end(), "/wallet/w1") == static_cast<long>(wallet.uris.size()));
    BOOST_CHECK_EQUAL(wallet.sent.size(), 2u);

    // The node's errors reach the caller.
    wallet.locked = true;
    BOOST_CHECK_EXCEPTION(send(client, AddressOf(carol), "1"), RPCError,
        [](const RPCError& e) { return e.code == RPC_WALLET_UNLOCK_NEEDED && e.message.find("passphrase") != std::string::npos; });
    wallet.locked = false;

    // Only alice's coins count, even if the wallet has more elsewhere.
    wallet.mine.insert(AddressOf(carol));
    wallet.coins = {{node.Fund(carol, 5 * COIN), CTxOut(5 * COIN, carol), AddressOf(carol)}};
    BOOST_CHECK_EXCEPTION(send(client, AddressOf(bob), "1"), RPCError,
        [](const RPCError& e) { return e.code == MP_ERR_INPUTSELECT_FAIL; });

    // Too little for bob's dust output and the fee (together about 11 500 sat).
    wallet.coins = {{node.Fund(alice, 8000), CTxOut(8000, alice), AddressOf(alice)}};
    BOOST_CHECK_EXCEPTION(send(client, AddressOf(bob), "1"), RPCError,
        [](const RPCError& e) { return e.code == MP_ERR_CREATE_TX && e.message.find("Insufficient funds") != std::string::npos; });

    // A payload too large for OP_RETURN goes out as Class B: Exodus output and
    // multisig outputs with the wallet's public key of alice.
    wallet.coins = {{node.Fund(alice, 1 * COIN), CTxOut(1 * COIN, alice), AddressOf(alice)}};
    {
        UniValue params(UniValue::VARR);
        params.push_back(AddressOf(alice));
        params.push_back(1);
        params.push_back(2);
        params.push_back(0);
        params.push_back("Games");
        params.push_back("");
        params.push_back("Big");
        params.push_back("");
        params.push_back(std::string(150, 'x'));
        params.push_back("10");
        client.Call("omni_sendissuancefixed", params);
    }
    CTransactionRef classB = wallet.sent.back();
    int multisig = 0, exodus = 0;
    for (const CTxOut& out : classB->vout) {
        txnouttype type;
        std::vector<std::vector<unsigned char>> solutions;
        type = Solver(out.scriptPubKey, solutions);
        if (type == TX_MULTISIG) ++multisig;
        if (out.scriptPubKey == GetScriptForDestination(ExodusAddress())) ++exodus;
    }
    BOOST_CHECK_GE(multisig, 1);
    BOOST_CHECK_EQUAL(exodus, 1);

    // omni_funded_send: the tokens' owner sends, a second address pays the fee
    // and gets the change. Sender's inputs first, change output first.
    const COutPoint aliceDust = node.Fund(alice, 10000);
    const COutPoint carolFunds = node.Fund(carol, 1 * COIN);
    wallet.mine.insert(AddressOf(carol));
    wallet.coins = {{aliceDust, CTxOut(10000, alice), AddressOf(alice)}, {carolFunds, CTxOut(1 * COIN, carol), AddressOf(carol)}};
    {
        UniValue params(UniValue::VARR);
        params.push_back(AddressOf(alice));
        params.push_back(AddressOf(bob));
        params.push_back(3);
        params.push_back("5");
        params.push_back(AddressOf(carol));
        client.Call("omni_funded_send", params);
    }
    CTransactionRef funded = wallet.sent.back();
    BOOST_REQUIRE_EQUAL(funded->vin.size(), 2u);
    BOOST_CHECK(funded->vin[0].prevout == aliceDust);
    BOOST_CHECK(funded->vin[1].prevout == carolFunds);
    BOOST_REQUIRE_EQUAL(funded->vout.size(), 3u);
    BOOST_CHECK(funded->vout[0].scriptPubKey == carol);
    BOOST_CHECK(funded->vout[1].scriptPubKey[0] == OP_RETURN);
    BOOST_CHECK(funded->vout[2].scriptPubKey == bob);
    Connect("s3", start + 2, {funded});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 105 * COIN);
    BOOST_CHECK_EQUAL(Balance(carol, 3), 0);

    // Without a node wallet: as Omni Core without one.
    SetWalletNode(nullptr);
    BOOST_CHECK_EXCEPTION(send(client, AddressOf(bob), "1"), RPCError,
        [](const RPCError& e) { return e.code == MP_ERR_WALLET_ACCESS; });

    node.mempoolTxs.clear();
    MempoolWatcher(node).Refresh();
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
