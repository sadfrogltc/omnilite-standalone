// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Runs blocks through OmniLite's handlers via the indexer, including a reorg
// and restarts, on regtest.

#include <indexer.h>

#include <chainparams.h>
#include <hash.h>
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
    bool unreachable = false;

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
        blockHash = uint256();
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
        for (const auto& tx : txs) node.txs[tx->GetHash()] = tx;
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

BOOST_AUTO_TEST_SUITE_END()
