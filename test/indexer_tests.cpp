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
#include <atomic>
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

    explicit Fixture(const std::string& chain = CBaseChainParams::REGTEST)
    {
        SelectParams(chain);
        start = mastercore::ConsensusParams().GENESIS_BLOCK;
        Open();
    }
    ~Fixture() { Close(); }

    void Open()
    {
        store = std::make_unique<CStore>(dir.Sub("index"), Params().NetworkIDString(), 100);
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

/** On mainnet: only there OmniLite sees DEx payments (its Exodus output is the mainnet one). */
struct MainFixture : Fixture {
    MainFixture() : Fixture(CBaseChainParams::MAIN) {}
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
    std::set<std::string> watchOnly;
    /** The wallet's transactions, oldest first, with their block (null: unconfirmed). */
    std::vector<std::pair<uint256, uint256>> history;
    std::vector<CTransactionRef> sent;
    std::vector<std::string> uris;
    int signs = 0;
    bool locked = false;
    /** As a node without the wallet asked for. */
    bool noWallet = false;
    /** The node's height (getblockcount). */
    std::atomic<int64_t> blockCount{0};

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
        if (noWallet) {
            error = Error(RPC_WALLET_NOT_FOUND, "Requested wallet does not exist or is not loaded");
            return true;
        }
        if (method == "getblockcount") {
            result = UniValue(static_cast<int64_t>(blockCount));
        } else if (method == "listreceivedbyaddress") {
            result = UniValue(UniValue::VARR);
            for (const std::string& a : mine) {
                UniValue r(UniValue::VOBJ);
                r.pushKV("address", a);
                result.push_back(r);
            }
            if (params[2].isTrue()) {
                for (const std::string& a : watchOnly) {
                    UniValue r(UniValue::VOBJ);
                    r.pushKV("involvesWatchonly", true);
                    r.pushKV("address", a);
                    result.push_back(r);
                }
            }
        } else if (method == "listtransactions") {
            // As the node: one entry per output, so a transaction may come twice.
            result = UniValue(UniValue::VARR);
            for (const auto& h : history) {
                for (int i = 0; i < 2; ++i) {
                    UniValue e(UniValue::VOBJ);
                    e.pushKV("txid", h.first.GetHex());
                    if (!h.second.IsNull()) e.pushKV("blockhash", h.second.GetHex());
                    result.push_back(e);
                }
            }
        } else if (method == "listunspent") {
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
            result.pushKV("iswatchonly", watchOnly.count(params[0].get_str()) > 0);
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

/** The API with a node wallet behind it that holds 5 LTC for each of the given scripts. */
struct WalletAPI {
    FakeWallet wallet;
    FakeLitecoind litecoind;
    RPCClient nodeRPC{"127.0.0.1", litecoind.port, "user", "pass", 10};
    std::unique_ptr<APIServer> api;
    std::unique_ptr<RPCClient> client;

    WalletAPI(FakeNode& node, std::initializer_list<const CScript*> funded)
    {
        for (const CScript* who : funded) {
            wallet.mine.insert(AddressOf(*who));
            const COutPoint coin = node.Fund(*who, 5 * COIN);
            wallet.coins.push_back({coin, node.txs[coin.hash]->vout[0], AddressOf(*who)});
        }
        litecoind.handler = [this](const std::string& m, const UniValue& p, const std::string& u, UniValue& r, UniValue& e) { return wallet.Handle(m, p, u, r, e); };
        RegisterOmniRPCCommands();
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        SetWalletNode(&nodeRPC);
        APIOptions options;
        options.user = "u";
        options.password = "p";
        api = std::make_unique<APIServer>(options, &nodeRPC);
        api->Start();
        client = std::make_unique<RPCClient>("127.0.0.1", api->Port(), "u", "p", 10);
    }
    ~WalletAPI()
    {
        api.reset();
        SetWalletNode(nullptr);
    }

    UniValue Call(const std::string& method, std::vector<UniValue> args)
    {
        UniValue params(UniValue::VARR);
        for (const UniValue& a : args) params.push_back(a);
        return client->Call(method, params);
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

    // A payload too large for OP_RETURN would go out as Class B, which OmniLite
    // does not see on regtest (Exodus is P2SH there): the dry run stops it.
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
        const size_t sentBefore = wallet.sent.size();
        BOOST_CHECK_EXCEPTION(client.Call("omni_sendissuancefixed", params), RPCError,
            [](const RPCError& e) { return e.message.find("would not see this as an Omni transaction") != std::string::npos; });
        BOOST_CHECK_EQUAL(wallet.sent.size(), sentBefore);
    }

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

BOOST_AUTO_TEST_CASE(wallet_calls_through_the_node_wallet)
{
    const CScript dave = ScriptFor(0xd4);
    std::vector<unsigned char> issue = CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN);
    CTransactionRef issueTx = node.OmniTx(alice, issue);
    Connect("w1", start, {issueTx});
    // Two of the wallet's transactions in one block.
    CTransactionRef toBob = node.OmniTx(alice, CreatePayload_SimpleSend(3, 400 * COIN), &bob);
    CTransactionRef toCarol = node.OmniTx(alice, CreatePayload_SimpleSend(3, 50 * COIN), &carol);
    Connect("w2", start + 1, {toBob, toCarol});
    // None of the wallet's addresses involved.
    CTransactionRef carolToDave = node.OmniTx(carol, CreatePayload_SimpleSend(3, 10 * COIN), &dave);
    Connect("w3", start + 2, {carolToDave});

    // alice is the wallet's, bob watch-only. Its history also has a plain payment.
    FakeWallet wallet;
    wallet.mine.insert(AddressOf(alice));
    wallet.watchOnly.insert(AddressOf(bob));
    const COutPoint payment = node.Fund(alice, 1 * COIN);
    for (const CTransactionRef& tx : {issueTx, toBob, toCarol}) wallet.history.push_back({tx->GetHash(), node.txBlock[tx->GetHash()]});
    wallet.history.push_back({payment.hash, uint256()});
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
    auto call = [&](const RPCClient& c, const std::string& method, std::vector<UniValue> args) {
        UniValue params(UniValue::VARR);
        for (const UniValue& a : args) params.push_back(a);
        return c.Call(method, params);
    };
    auto txids = [](const UniValue& list) {
        std::vector<std::string> ids;
        for (const UniValue& tx : list.getValues()) ids.push_back(find_value(tx, "txid").get_str());
        return ids;
    };

    // omni_listtransactions: the wallet's Omni transactions, newest first, in
    // block order within a block; the plain payment and carol's send are not there.
    UniValue list = call(client, "omni_listtransactions", {});
    std::vector<std::string> expected{toCarol->GetHash().GetHex(), toBob->GetHash().GetHex(), issueTx->GetHash().GetHex()};
    BOOST_CHECK(txids(list) == expected);
    for (const UniValue& tx : list.getValues()) BOOST_CHECK(find_value(tx, "ismine").isTrue());
    BOOST_CHECK_EQUAL(find_value(list[0], "amount").get_str(), "50.00000000");

    // count, skip and the address filter as in OmniLite.
    BOOST_CHECK(txids(call(client, "omni_listtransactions", {UniValue("*"), UniValue(1)})) == std::vector<std::string>{toCarol->GetHash().GetHex()});
    BOOST_CHECK(txids(call(client, "omni_listtransactions", {UniValue("*"), UniValue(1), UniValue(1)})) == std::vector<std::string>{toBob->GetHash().GetHex()});
    // Ordering within a block asks the node for the block; that leaves no trace in later answers.
    BOOST_CHECK_EQUAL(find_value(call(client, "omni_gettransaction", {UniValue(issueTx->GetHash().GetHex())}), "confirmations").get_int(), 3);
    BOOST_CHECK(txids(call(client, "omni_listtransactions", {UniValue(AddressOf(bob))})) == std::vector<std::string>{toBob->GetHash().GetHex()});

    // "ismine" in other calls: from the wallet's addresses only.
    BOOST_CHECK(find_value(call(client, "omni_gettransaction", {UniValue(toBob->GetHash().GetHex())}), "ismine").isTrue());
    BOOST_CHECK(find_value(call(client, "omni_gettransaction", {UniValue(carolToDave->GetHash().GetHex())}), "ismine").isFalse());

    // Balances: alice has 550 left; with watch-only, bob's 400 count too.
    UniValue balances = call(client, "omni_getwalletbalances", {});
    BOOST_REQUIRE_EQUAL(balances.size(), 1u);
    BOOST_CHECK_EQUAL(find_value(balances[0], "propertyid").get_int(), 3);
    BOOST_CHECK_EQUAL(find_value(balances[0], "balance").get_str(), "550.00000000");
    UniValue withWatchOnly;
    withWatchOnly.setBool(true);
    BOOST_CHECK_EQUAL(find_value(call(client, "omni_getwalletbalances", {withWatchOnly})[0], "balance").get_str(), "950.00000000");

    UniValue byAddress = call(client, "omni_getwalletaddressbalances", {});
    BOOST_REQUIRE_EQUAL(byAddress.size(), 1u);
    BOOST_CHECK_EQUAL(find_value(byAddress[0], "address").get_str(), AddressOf(alice));
    BOOST_CHECK_EQUAL(call(client, "omni_getwalletaddressbalances", {withWatchOnly}).size(), 2u);

    // The wallet in the URI is the one asked.
    wallet.uris.clear();
    call(client.ForWallet("w1"), "omni_getwalletbalances", {});
    BOOST_CHECK(!wallet.uris.empty());
    BOOST_CHECK(std::count(wallet.uris.begin(), wallet.uris.end(), "/wallet/w1") == static_cast<long>(wallet.uris.size()));

    // Without that wallet: the wallet calls report the node's error, the
    // others work and nothing is "mine".
    wallet.noWallet = true;
    BOOST_CHECK_EXCEPTION(call(client, "omni_listtransactions", {}), RPCError,
        [](const RPCError& e) { return e.code == RPC_WALLET_NOT_FOUND; });
    BOOST_CHECK(find_value(call(client, "omni_gettransaction", {UniValue(toBob->GetHash().GetHex())}), "ismine").isFalse());

    SetWalletNode(nullptr);
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

/**
 * Token management as a user does it: each call goes through the API, the
 * node's wallet sends, the transaction is mined and OmniLite books it.
 */
BOOST_AUTO_TEST_CASE(token_management_through_the_api)
{
    const CScript dave = ScriptFor(0xd4);
    const std::string A = AddressOf(alice), B = AddressOf(bob), C = AddressOf(carol), D = AddressOf(dave);
    FakeWallet wallet;
    for (const CScript* who : {&alice, &bob, &carol}) {
        wallet.mine.insert(AddressOf(*who));
        const COutPoint coin = node.Fund(*who, 5 * COIN);
        wallet.coins.push_back({coin, node.txs[coin.hash]->vout[0], AddressOf(*who)});
    }
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

    // A first block: before any, no input type is allowed yet (height -1).
    Connect("t-start", start, {});
    int64_t height = start + 1;
    auto call = [&](const std::string& method, std::vector<UniValue> args) {
        UniValue params(UniValue::VARR);
        for (const UniValue& a : args) params.push_back(a);
        return client.Call(method, params);
    };
    // Sends through the API, mines the transaction in the next block and
    // returns it as OmniLite sees it.
    auto sendAndMine = [&](const std::string& method, std::vector<UniValue> args) {
        BOOST_TEST_CHECKPOINT(method);
        const size_t before = wallet.sent.size();
        call(method, args);
        BOOST_REQUIRE_EQUAL(wallet.sent.size(), before + 1);
        CTransactionRef tx = wallet.sent.back();
        Connect(strprintf("t%d", height), height, {tx});
        ++height;
        UniValue info = call("omni_gettransaction", {UniValue(tx->GetHash().GetHex())});
        BOOST_CHECK_MESSAGE(find_value(info, "valid").isTrue(), method << ": " << info.write());
        return info;
    };
    auto property = [&](int id) { return call("omni_getproperty", {UniValue(id)}); };
    auto balanceField = [&](const std::string& who, int id, const std::string& field) {
        return find_value(call("omni_getbalance", {UniValue(who), UniValue(id)}), field).get_str();
    };
    UniValue no;
    no.setBool(false);

    // Managed property 3: alice is the issuer, nothing issued yet.
    sendAndMine("omni_sendissuancemanaged", {UniValue(A), UniValue(1), UniValue(MSC_PROPERTY_TYPE_DIVISIBLE), UniValue(0),
        UniValue("Companies"), UniValue("Mining"), UniValue("Quantum"), UniValue("example.com"), UniValue("Quantum tokens")});
    BOOST_CHECK(find_value(property(3), "managedissuance").isTrue());
    BOOST_CHECK_EQUAL(find_value(property(3), "issuer").get_str(), A);
    BOOST_CHECK_EQUAL(find_value(property(3), "totaltokens").get_str(), "0.00000000");

    // Only the issuer may grant: OmniLite refuses before anything is built or sent.
    BOOST_CHECK_THROW(call("omni_sendgrant", {UniValue(B), UniValue(B), UniValue(3), UniValue("1")}), RPCError);
    BOOST_CHECK_EQUAL(wallet.sent.size(), 1u);

    // Grants to bob and to the issuer herself, a revoke from her.
    sendAndMine("omni_sendgrant", {UniValue(A), UniValue(B), UniValue(3), UniValue("1000"), UniValue("first")});
    sendAndMine("omni_sendgrant", {UniValue(A), UniValue(""), UniValue(3), UniValue("500")});
    sendAndMine("omni_sendrevoke", {UniValue(A), UniValue(3), UniValue("200")});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 1000 * COIN);
    BOOST_CHECK_EQUAL(Balance(alice, 3), 300 * COIN);
    BOOST_CHECK_EQUAL(find_value(property(3), "totaltokens").get_str(), "1300.00000000");
    BOOST_CHECK_EQUAL(find_value(call("omni_getgrants", {UniValue(3)}), "issuances").size(), 3u);

    // Freezing: enabled, used, undone. Without the freeze notice feature (not active
    // on mainnet either) there is no waiting period.
    sendAndMine("omni_sendenablefreezing", {UniValue(A), UniValue(3)});
    sendAndMine("omni_sendfreeze", {UniValue(A), UniValue(B), UniValue(3), UniValue("1000")});
    BOOST_CHECK_EQUAL(balanceField(B, 3, "frozen"), "1000.00000000");
    sendAndMine("omni_sendunfreeze", {UniValue(A), UniValue(B), UniValue(3), UniValue("1000")});
    BOOST_CHECK_EQUAL(balanceField(B, 3, "frozen"), "0.00000000");
    sendAndMine("omni_senddisablefreezing", {UniValue(A), UniValue(3)});
    BOOST_CHECK(!mastercore::isFreezingEnabled(3, height));

    // Send to owners: bob is the only other holder. The fee is paid in OMNI,
    // which alice gets here directly.
    {
        LOCK(cs_tally);
        BOOST_REQUIRE(mastercore::update_tally_map(A, OMNI_PROPERTY_MSC, COIN, BALANCE));
    }
    UniValue sto = sendAndMine("omni_sendsto", {UniValue(A), UniValue(3), UniValue("100")});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 1100 * COIN);
    BOOST_CHECK_EQUAL(Balance(alice, 3), 200 * COIN);
    BOOST_CHECK_EQUAL(Balance(alice, OMNI_PROPERTY_MSC), COIN - 1);
    BOOST_CHECK_EQUAL(find_value(call("omni_getsto", {find_value(sto, "txid"), UniValue("*")}), "recipients").size(), 1u);

    // A crowdsale of carol for property 3: bob takes part, carol closes it.
    sendAndMine("omni_sendissuancecrowdsale", {UniValue(C), UniValue(1), UniValue(MSC_PROPERTY_TYPE_DIVISIBLE), UniValue(0),
        UniValue("Companies"), UniValue("Mining"), UniValue("Crowd"), UniValue(""), UniValue(""),
        UniValue(3), UniValue("2"), UniValue(int64_t{2000000000}), UniValue(0), UniValue(0)});
    BOOST_CHECK(find_value(call("omni_getcrowdsale", {UniValue(4)}), "active").isTrue());
    sendAndMine("omni_send", {UniValue(B), UniValue(C), UniValue(3), UniValue("50")});
    BOOST_CHECK_EQUAL(Balance(bob, 4), 100 * COIN);
    BOOST_CHECK_EQUAL(Balance(carol, 3), 50 * COIN);
    sendAndMine("omni_sendclosecrowdsale", {UniValue(C), UniValue(4)});
    BOOST_CHECK(find_value(call("omni_getcrowdsale", {UniValue(4)}), "active").isFalse());

    // A new issuer for property 3, and all of bob's tokens to dave.
    sendAndMine("omni_sendchangeissuer", {UniValue(A), UniValue(D), UniValue(3)});
    BOOST_CHECK_EQUAL(find_value(property(3), "issuer").get_str(), D);
    sendAndMine("omni_sendall", {UniValue(B), UniValue(D), UniValue(1)});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 0);
    BOOST_CHECK_EQUAL(Balance(bob, 4), 0);
    BOOST_CHECK_EQUAL(Balance(dave, 3), 1050 * COIN);
    BOOST_CHECK_EQUAL(Balance(dave, 4), 100 * COIN);

    // Non-fungible tokens: issued, three sent to bob, who sets their data.
    sendAndMine("omni_sendissuancemanaged", {UniValue(A), UniValue(1), UniValue(MSC_PROPERTY_TYPE_NONFUNGIBLE), UniValue(0),
        UniValue("Art"), UniValue(""), UniValue("Pictures"), UniValue(""), UniValue("")});
    sendAndMine("omni_sendgrant", {UniValue(A), UniValue(A), UniValue(5), UniValue("10")});
    sendAndMine("omni_sendnonfungible", {UniValue(A), UniValue(B), UniValue(5), UniValue(1), UniValue(3)});
    BOOST_CHECK_EQUAL(Balance(bob, 5), 3);
    BOOST_CHECK_EQUAL(Balance(alice, 5), 7);
    sendAndMine("omni_setnonfungibledata", {UniValue(5), UniValue(1), UniValue(3), no, UniValue("bob's")});
    UniValue nftData = call("omni_getnonfungibletokendata", {UniValue(5), UniValue(1), UniValue(3)});
    BOOST_REQUIRE_EQUAL(nftData.size(), 3u);
    for (const UniValue& token : nftData.getValues()) {
        BOOST_CHECK_EQUAL(find_value(token, "owner").get_str(), B);
        BOOST_CHECK_EQUAL(find_value(token, "holderdata").get_str(), "bob's");
    }

    // Arbitrary data.
    BOOST_CHECK_EQUAL(find_value(sendAndMine("omni_sendanydata", {UniValue(A), UniValue("68656c6c6f")}), "data").get_str(), "68656c6c6f");

    SetWalletNode(nullptr);
}

namespace {

/** Whether the call fails with a message that contains text. */
bool FailsWith(const std::function<void()>& call, const std::string& text)
{
    try {
        call();
    } catch (const RPCError& e) {
        BOOST_TEST_MESSAGE(e.message);
        return e.message.find(text) != std::string::npos;
    }
    return false;
}

} // namespace

BOOST_FIXTURE_TEST_CASE(dex_sell_accept_pay_with_the_guard, MainFixture)
{
    const std::string A = AddressOf(alice), B = AddressOf(bob);
    // alice issues properties 3 and 4.
    Connect("d0", start, {
        node.OmniTx(alice, CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Coin", "", "", 1000 * COIN)),
        node.OmniTx(alice, CreatePayload_IssuanceFixed(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Other", "", "", 1000 * COIN))});
    int64_t height = start + 1;
    WalletAPI w(node, {&alice, &bob});
    w.wallet.blockCount = start;
    auto mine = [&](std::vector<CTransactionRef> txs) {
        Connect(strprintf("d%d", height), height, txs);
        w.wallet.blockCount = height;
        return height++;
    };
    auto sendAndMine = [&](const std::string& method, std::vector<UniValue> args) {
        BOOST_TEST_CHECKPOINT(method);
        const size_t before = w.wallet.sent.size();
        w.Call(method, args);
        BOOST_REQUIRE_EQUAL(w.wallet.sent.size(), before + 1);
        const int64_t block = mine({w.wallet.sent.back()});
        BOOST_CHECK_MESSAGE(find_value(w.Call("omni_gettransaction", {UniValue(w.wallet.sent.back()->GetHash().GetHex())}), "valid").isTrue(), method);
        return block;
    };
    auto pay = [&](const std::string& seller, int property, const std::string& amount) {
        return w.Call("omni_senddexpay", {UniValue(B), UniValue(seller), UniValue(property), UniValue(amount)});
    };

    // alice sells 10 of property 3 for 1 LTC, payment window 10 blocks; bob accepts 4.
    sendAndMine("omni_senddexsell", {UniValue(A), UniValue(3), UniValue("10"), UniValue("1"), UniValue(10), UniValue("0.0001"), UniValue(1)});
    BOOST_CHECK_EQUAL(w.Call("omni_getactivedexsells", {}).size(), 1u);
    sendAndMine("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(3), UniValue("4")});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 990 * COIN); // 10 reserved for the offer
    const size_t sent = w.wallet.sent.size();

    // OmniLite behind the node: the window would be counted wrong.
    w.wallet.blockCount = height;
    BOOST_CHECK(FailsWith([&] { pay(A, 3, "0.4"); }, "caught up"));
    w.wallet.blockCount = height - 1;

    // The payment, then no second one while it is unconfirmed.
    const std::string txid = pay(A, 3, "0.4").get_str();
    BOOST_REQUIRE_EQUAL(w.wallet.sent.size(), sent + 1);
    BOOST_CHECK(FailsWith([&] { pay(A, 3, "0.4"); }, "still unconfirmed"));
    mine({w.wallet.sent.back()});
    BOOST_CHECK_EQUAL(Balance(bob, 3), 4 * COIN);
    UniValue payment = w.Call("omni_gettransaction", {UniValue(txid)});
    BOOST_CHECK_EQUAL(find_value(payment, "type").get_str(), "DEx Purchase");
    BOOST_CHECK(find_value(find_value(payment, "purchases")[0], "valid").isTrue());

    // A second accept, left until only 2 blocks of its window remain.
    const int64_t accepted2 = sendAndMine("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(3), UniValue("2")});
    while (height <= accepted2 + 8) mine({});
    BOOST_CHECK(FailsWith([&] { pay(A, 3, "0.2"); }, "Only 2 block(s) of the payment window left"));

    // Once accept 2 has run out, a third one; then alice cancels her offer. The
    // accept stays, but OmniLite (free DEx) would not book a payment any more.
    while (height <= accepted2 + 10) mine({});
    sendAndMine("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(3), UniValue("1")});
    sendAndMine("omni_senddexsell", {UniValue(A), UniValue(3), UniValue("0"), UniValue("0"), UniValue(0), UniValue("0"), UniValue(3)});
    BOOST_CHECK(FailsWith([&] { pay(A, 3, "0.1"); }, "no offer anymore"));
    // Her new offer for property 4 would take a payment meant for property 3.
    sendAndMine("omni_senddexsell", {UniValue(A), UniValue(4), UniValue("5"), UniValue("1"), UniValue(10), UniValue("0.0001"), UniValue(1)});
    BOOST_CHECK(FailsWith([&] { pay(A, 3, "0.1"); }, "now offers property 4"));

    // None of the refused payments went out.
    BOOST_CHECK_EQUAL(w.wallet.sent.back()->GetHash().GetHex(), node.blocks[Hash(strprintf("d%d", height - 1))][0]->GetHash().GetHex());
}

BOOST_AUTO_TEST_CASE(dex_payment_refused_where_omnilite_cannot_see_it)
{
    // Regtest: without free DEx only OMNI is sold; alice gets some directly.
    const std::string A = AddressOf(alice), B = AddressOf(bob);
    Connect("r0", start, {});
    {
        LOCK(cs_tally);
        BOOST_REQUIRE(mastercore::update_tally_map(A, OMNI_PROPERTY_MSC, 100 * COIN, BALANCE));
    }
    int64_t height = start + 1;
    WalletAPI w(node, {&alice, &bob});
    w.wallet.blockCount = start;
    auto sendAndMine = [&](const std::string& method, std::vector<UniValue> args) {
        w.Call(method, args);
        Connect(strprintf("r%d", height), height, {w.wallet.sent.back()});
        w.wallet.blockCount = height++;
    };
    sendAndMine("omni_senddexsell", {UniValue(A), UniValue(1), UniValue("10"), UniValue("1"), UniValue(10), UniValue("0.0001"), UniValue(1)});
    sendAndMine("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(1), UniValue("4")});
    const size_t sent = w.wallet.sent.size();

    // The Exodus output of regtest is P2SH: OmniLite would never see the payment.
    BOOST_CHECK(FailsWith([&] { w.Call("omni_senddexpay", {UniValue(B), UniValue(A), UniValue(1), UniValue("0.4")}); }, "never seen"));
    BOOST_CHECK_EQUAL(w.wallet.sent.size(), sent);
}

BOOST_AUTO_TEST_CASE(dry_run_stops_what_omnilite_would_reject)
{
    // Cases OmniLite's own call checks let through, its logic would reject.
    const std::string A = AddressOf(alice), B = AddressOf(bob);
    Connect("v0", start, {node.OmniTx(alice, CreatePayload_IssuanceManaged(1, MSC_PROPERTY_TYPE_DIVISIBLE, 0, "Games", "", "Managed", "", ""))});
    {
        LOCK(cs_tally);
        BOOST_REQUIRE(mastercore::update_tally_map(A, OMNI_PROPERTY_MSC, 100 * COIN, BALANCE));
    }
    int64_t height = start + 1;
    WalletAPI w(node, {&alice, &bob});
    auto refused = [&](const std::string& method, std::vector<UniValue> args, const std::string& reason) {
        const size_t before = w.wallet.sent.size();
        const bool failed = FailsWith([&] { w.Call(method, args); }, reason);
        return failed && w.wallet.sent.size() == before;
    };
    auto sendAndMine = [&](const std::string& method, std::vector<UniValue> args) {
        w.Call(method, args);
        Connect(strprintf("v%d", height), height, {w.wallet.sent.back()});
        ++height;
    };

    // Freezing is not enabled for property 3.
    BOOST_CHECK(refused("omni_sendfreeze", {UniValue(A), UniValue(B), UniValue(3), UniValue("1")}, "freezing is not enabled"));
    // Also when only asked for the signed transaction (omni_setautocommit false).
    UniValue off;
    off.setBool(false);
    w.Call("omni_setautocommit", {off});
    BOOST_CHECK(refused("omni_sendfreeze", {UniValue(A), UniValue(B), UniValue(3), UniValue("1")}, "freezing is not enabled"));
    UniValue on;
    on.setBool(true);
    w.Call("omni_setautocommit", {on});
    // bob has no tokens to send.
    BOOST_CHECK(refused("omni_sendall", {UniValue(B), UniValue(A), UniValue(1)}, "no tokens to send"));

    // A second accept of the same offer by the same buyer.
    sendAndMine("omni_senddexsell", {UniValue(A), UniValue(1), UniValue("10"), UniValue("1"), UniValue(10), UniValue("0.0001"), UniValue(1)});
    sendAndMine("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(1), UniValue("4")});
    BOOST_CHECK(refused("omni_senddexaccept", {UniValue(B), UniValue(A), UniValue(1), UniValue("1")}, "already exists"));

    // What OmniLite accepts still goes out.
    sendAndMine("omni_sendenablefreezing", {UniValue(A), UniValue(3)});
    BOOST_CHECK(mastercore::isFreezingEnabled(3, height));
}

/** Class B: the Exodus output OmniLite knows is the mainnet one. */
BOOST_FIXTURE_TEST_CASE(class_b_on_mainnet, MainFixture)
{
    const std::string A = AddressOf(alice);
    Connect("b0", start, {});
    WalletAPI w(node, {&alice});
    // Too large for OP_RETURN: Exodus output and multisig outputs with alice's public key from the wallet.
    w.Call("omni_sendissuancefixed", {UniValue(A), UniValue(1), UniValue(MSC_PROPERTY_TYPE_DIVISIBLE), UniValue(0),
        UniValue("Games"), UniValue(""), UniValue("Big"), UniValue(""), UniValue(std::string(150, 'x')), UniValue("10")});
    BOOST_REQUIRE_EQUAL(w.wallet.sent.size(), 1u);
    CTransactionRef classB = w.wallet.sent.back();
    int multisig = 0, exodus = 0;
    for (const CTxOut& out : classB->vout) {
        std::vector<std::vector<unsigned char>> solutions;
        if (Solver(out.scriptPubKey, solutions) == TX_MULTISIG) ++multisig;
        if (out.scriptPubKey == GetScriptForDestination(ExodusAddress())) ++exodus;
    }
    BOOST_CHECK_GE(multisig, 1);
    BOOST_CHECK_EQUAL(exodus, 1);

    // OmniLite books it.
    Connect("b1", start + 1, {classB});
    BOOST_CHECK_EQUAL(Balance(alice, 3), 10 * COIN);
    BOOST_CHECK_EQUAL(find_value(w.Call("omni_getproperty", {UniValue(3)}), "data").get_str(), std::string(150, 'x'));
}

BOOST_AUTO_TEST_SUITE_END()
