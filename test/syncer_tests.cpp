// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <syncer.h>

#include <test/util.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <thread>
#include <vector>

namespace {

/** A node whose best chain can be replaced to simulate reorgs; hashes[i] is the block at height i. */
class FakeChain : public ChainSource
{
public:
    FakeChain(const std::string& tag, int n) { Extend(tag, n); }

    void Extend(const std::string& tag, int n)
    {
        while (static_cast<int>(hashes.size()) < n) hashes.push_back(tag + "-" + std::to_string(hashes.size()));
    }

    /** Replaces everything from height on with a new branch of total length n. */
    void Fork(const std::string& tag, int height, int n)
    {
        hashes.resize(height);
        Extend(tag, n);
    }

    int64_t GetBlockCount() override { return static_cast<int64_t>(hashes.size()) - 1; }

    std::string GetBlockHash(int64_t h) override
    {
        if (h < 0 || h >= static_cast<int64_t>(hashes.size())) throw HeightOutOfRange("Block height out of range");
        return hashes[h];
    }

    BlockHeader GetBlockHeader(const std::string& hash) override
    {
        for (size_t h = 0; h < hashes.size(); ++h) {
            if (hashes[h] == hash) return {hash, static_cast<int64_t>(h), h > 0 ? hashes[h - 1] : "", 0};
        }
        throw NodeError("Block not found");
    }

    std::vector<std::string> hashes;
};

/** Stores the hash per height and a running block counter, so the test sees
 *  whether the state follows the active chain exactly. */
class CountingProcessor : public Processor
{
public:
    void ConnectBlock(CBlockTx& tx, const BlockHeader& header) override
    {
        tx.Put("blk", std::to_string(header.height), header.hash);
        std::string n;
        tx.Put("cnt", "n", std::to_string(tx.Get("cnt", "n", n) ? std::stoi(n) + 1 : 1));
    }
};

const int64_t START = 3;

struct Fixture {
    TempDir dir;
    CStore store;
    CountingProcessor processor;
    explicit Fixture(int64_t undoDepth = 100) : store(dir.Sub("db"), "regtest", undoDepth) {}

    void SyncAll(FakeChain& chain)
    {
        Syncer s(chain, store, processor, START, std::chrono::milliseconds(0));
        for (int i = 0; i < 1000; ++i) {
            if (!s.Step()) return;
        }
        BOOST_FAIL("sync did not settle");
    }

    /** Checks that store blocks and state equal the chain from START on. */
    void CheckMatches(const FakeChain& chain)
    {
        int64_t wantTip = static_cast<int64_t>(chain.hashes.size()) - 1;
        std::optional<BlockInfo> tip = store.Tip();
        BOOST_REQUIRE(tip);
        BOOST_CHECK_EQUAL(tip->height, wantTip);
        BOOST_CHECK_EQUAL(tip->hash, chain.hashes[wantTip]);
        for (int64_t h = START; h <= wantTip; ++h) {
            std::string v;
            BOOST_CHECK(store.Get("blk", std::to_string(h), v));
            BOOST_CHECK_EQUAL(v, chain.hashes[h]);
            BOOST_CHECK_EQUAL(store.BlockAt(h)->hash, chain.hashes[h]);
        }
        BOOST_CHECK(!store.BlockAt(wantTip + 1));
        std::string n;
        BOOST_CHECK(store.Get("cnt", "n", n));
        BOOST_CHECK_EQUAL(n, std::to_string(wantTip - START + 1));
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(syncer_tests)

BOOST_AUTO_TEST_CASE(sync_from_start)
{
    Fixture f;
    FakeChain chain("a", 11);
    f.SyncAll(chain);
    f.CheckMatches(chain);
    chain.Extend("a", 15);
    f.SyncAll(chain);
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_CASE(reorg_to_longer_chain)
{
    Fixture f;
    FakeChain chain("a", 11);
    f.SyncAll(chain);
    chain.Fork("b", 7, 13);
    f.SyncAll(chain);
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_CASE(reorg_to_shorter_chain)
{
    Fixture f;
    FakeChain chain("a", 13);
    f.SyncAll(chain);
    chain.Fork("c", 5, 9);
    f.SyncAll(chain);
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_CASE(reorg_same_height)
{
    Fixture f;
    FakeChain chain("a", 11);
    f.SyncAll(chain);
    chain.Fork("d", 10, 11);
    f.SyncAll(chain);
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_CASE(reorg_below_start)
{
    Fixture f;
    FakeChain chain("a", 11);
    f.SyncAll(chain);
    chain.Fork("e", 1, 11);
    f.SyncAll(chain);
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_CASE(reorg_deeper_than_undo)
{
    Fixture f(10);
    FakeChain chain("a", 40);
    f.SyncAll(chain);
    chain.Fork("f", 20, 41);
    Syncer s(chain, f.store, f.processor, START, std::chrono::milliseconds(0));
    BOOST_CHECK_THROW(
        {
            for (int i = 0; i < 100; ++i) s.Step();
        },
        UndoUnavailableError);
}

BOOST_AUTO_TEST_CASE(run_stops_and_retries)
{
    // A node that fails twice, then serves the chain.
    class Flaky : public FakeChain
    {
    public:
        using FakeChain::FakeChain;
        int failures = 2;
        int64_t GetBlockCount() override
        {
            if (failures > 0) {
                --failures;
                throw NodeError("connection refused");
            }
            return FakeChain::GetBlockCount();
        }
    };
    Fixture f;
    Flaky chain("a", 11);
    std::atomic<bool> stop{false};
    Syncer s(chain, f.store, f.processor, START, std::chrono::milliseconds(10));
    std::thread t([&] { s.Run(stop); });
    for (int i = 0; i < 500 && (!f.store.Tip() || f.store.Tip()->height < 10); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stop = true;
    t.join();
    f.CheckMatches(chain);
}

BOOST_AUTO_TEST_SUITE_END()
