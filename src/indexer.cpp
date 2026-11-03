// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <indexer.h>

#include <chain.h>
#include <chainparams.h>
#include <logging.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <rpcclient.h>
#include <store.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>
#include <version.h>

// OmniLite's handlers (omnicore.cpp), declared in Core's validation.cpp.
int mastercore_handler_block_begin(int nBlockNow, CBlockIndex const* pBlockIndex);
int mastercore_handler_block_end(int nBlockNow, CBlockIndex const* pBlockIndex, unsigned int);
bool mastercore_handler_tx(const CTransaction& tx, int nBlock, unsigned int idx, CBlockIndex const* pBlockIndex, std::shared_ptr<std::map<COutPoint, Coin>> removedCoins);
void mastercore_handler_disc_begin(const int nHeight);

CTransactionRef DecodeTx(const std::string& hex)
{
    if (!IsHex(hex)) throw std::runtime_error("transaction is not hex");
    std::vector<unsigned char> data = ParseHex(hex);
    CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
    CMutableTransaction mtx;
    ss >> mtx;
    if (!ss.empty()) throw std::runtime_error("extra data after the transaction");
    return MakeTransactionRef(std::move(mtx));
}

// --- RPCNodeData ---

std::vector<CTransactionRef> RPCNodeData::GetBlockTxs(const std::string& hash)
{
    UniValue params(UniValue::VARR);
    params.push_back(hash);
    params.push_back(2);
    UniValue block;
    try {
        block = m_node.Call("getblock", params);
    } catch (const RPCError& e) {
        throw NodeError("getblock " + hash + ": " + e.what());
    } catch (const NodeUnreachable& e) {
        throw NodeError(e.what());
    }
    if (find_value(block, "hash").get_str() != hash) throw NodeError("node returned another block for " + hash);

    std::vector<CTransactionRef> txs;
    const UniValue blockTxs = find_value(block, "tx");
    for (const UniValue& t : blockTxs.getValues()) {
        const std::string txid = find_value(t, "txid").get_str();
        try {
            CTransactionRef ptx = DecodeTx(find_value(t, "hex").get_str());
            if (ptx->GetHash().GetHex() != txid) throw std::runtime_error("decodes to " + ptx->GetHash().GetHex());
            txs.push_back(ptx);
        } catch (const std::exception& e) {
            // Transactions with MWEB data (Litecoin 0.21) do not exist for
            // OmniLite 0.18; the position in the block still counts.
            LogWarn("block %s: transaction %s skipped, not readable as Litecoin 0.18 (%s)", hash, txid, e.what());
            txs.push_back(nullptr);
        }
    }
    return txs;
}

bool RPCNodeData::GetTx(const uint256& txid, CTransactionRef& tx, uint256& blockHash)
{
    UniValue params(UniValue::VARR);
    params.push_back(txid.GetHex());
    params.push_back(true);
    UniValue r;
    try {
        r = m_node.Call("getrawtransaction", params);
    } catch (const RPCError& e) {
        if (e.code == RPC_INVALID_ADDRESS_OR_KEY) return false; // unknown transaction
        throw NodeError("getrawtransaction " + txid.GetHex() + ": " + e.what());
    } catch (const NodeUnreachable& e) {
        throw NodeError(e.what());
    }
    tx = DecodeTx(find_value(r, "hex").get_str());
    const UniValue hash = find_value(r, "blockhash");
    blockHash = hash.isStr() ? uint256S(hash.get_str()) : uint256();
    return true;
}

// --- OmniIndexer ---

OmniIndexer::OmniIndexer(NodeData& node, CStore& store) : m_node(node), m_store(store) {}

OmniIndexer::~OmniIndexer()
{
    g_readBlock = nullptr;
    g_getTransaction = nullptr;
    if (m_started) mastercore_shutdown();
    LOCK(cs_main);
    chainActive.SetTip(nullptr);
    mapBlockIndex.clear();
}

CBlockIndex* OmniIndexer::AddIndex(int64_t height, const std::string& hash, int64_t time)
{
    uint256 h = uint256S(hash);
    auto it = mapBlockIndex.find(h);
    if (it != mapBlockIndex.end()) return it->second; // seen before, e.g. a block that comes back after a reorg
    m_index.emplace_back();
    CBlockIndex* pindex = &m_index.back();
    pindex->nHeight = static_cast<int>(height);
    pindex->nTime = static_cast<uint32_t>(time);
    pindex->pprev = chainActive.Tip();
    it = mapBlockIndex.emplace(h, pindex).first;
    pindex->phashBlock = &it->first;
    return pindex;
}

void OmniIndexer::Start(const fs::path& dataDir, const OmniOptions& options)
{
    fs::create_directories(dataDir);
    SetDataDir(dataDir);
    gArgs.ClearArgs();
    if (!options.activationAllowSenders.empty()) gArgs.ForceSetMultiArg("-omniactivationallowsender", options.activationAllowSenders);
    if (!options.alertAllowSenders.empty()) gArgs.ForceSetMultiArg("-omnialertallowsender", options.alertAllowSenders);
    if (!options.debug.empty()) gArgs.ForceSetMultiArg("-omnidebug", options.debug);

    g_readBlock = [this](const CBlockIndex* pindex, CBlock& block) { return ReadBlock(pindex, block.vtx); };
    g_getTransaction = [this](const uint256& txid, CTransactionRef& tx, uint256& blockHash) { return GetTransaction(txid, tx, blockHash); };

    // The active chain is what the store holds: OmniLite's state belongs to it.
    {
        LOCK(cs_main);
        std::optional<BlockInfo> tip = m_store.Tip();
        if (tip) {
            for (int64_t h = Params().OmniGenesisBlock(); h <= tip->height; ++h) {
                std::optional<BlockInfo> b = m_store.BlockAt(h);
                if (!b) continue; // the store may start later on test networks
                chainActive.SetTip(AddIndex(b->height, b->hash, b->time));
            }
        }
    }

    m_started = true;
    try {
        mastercore_init();
    } catch (const NodeError& e) {
        ClearBlockCaches();
        throw FatalError(std::string("loading the Omni state: ") + e.what());
    }
    ClearBlockCaches();
    if (NodeAborted()) throw FatalError("OmniLite stopped while loading its state, see the log");
}

void OmniIndexer::ClearBlockCaches()
{
    m_blockTxs.clear();
    m_replayTxs.clear();
    m_prefetched.clear();
}

bool OmniIndexer::ReadBlock(const CBlockIndex* pindex, std::vector<CTransactionRef>& txs)
{
    // OmniLite replays blocks after a reorg or at start-up; the node must
    // answer, a missing block would leave the state incomplete.
    txs = m_node.GetBlockTxs(pindex->GetBlockHash().GetHex());
    m_replayTxs.clear();
    for (const CTransactionRef& tx : txs) {
        if (tx) m_replayTxs[tx->GetHash()] = tx;
    }
    return true;
}

bool OmniIndexer::GetTransaction(const uint256& txid, CTransactionRef& tx, uint256& blockHash)
{
    // The caches only hold something while a block is being processed;
    // API calls in between go to the node.
    for (const auto* txs : {&m_blockTxs, &m_replayTxs}) {
        auto it = txs->find(txid);
        if (it != txs->end()) {
            tx = it->second;
            blockHash = uint256(); // the block being processed
            return true;
        }
    }
    auto pit = m_prefetched.find(txid);
    if (pit != m_prefetched.end()) {
        tx = pit->second.first;
        blockHash = pit->second.second;
        return tx != nullptr;
    }
    return m_node.GetTx(txid, tx, blockHash);
}

OmniIndexer::Prefetched OmniIndexer::Prefetch(const std::vector<CTransactionRef>& txs, const std::map<uint256, CTransactionRef>& blockTxs, int64_t height)
{
    Prefetched prefetched;
    for (const CTransactionRef& tx : txs) {
        if (!tx || tx->IsCoinBase()) continue;
        if (mastercore::GetEncodingClass(*tx, static_cast<int>(height)) == NO_MARKER) continue;
        for (const CTxIn& in : tx->vin) {
            const uint256& h = in.prevout.hash;
            if (blockTxs.count(h) || prefetched.count(h)) continue;
            CTransactionRef prev;
            uint256 blockHash;
            if (!m_node.GetTx(h, prev, blockHash)) prev = nullptr; // OmniLite then rejects the transaction (-101)
            prefetched[h] = {prev, blockHash};
        }
    }
    return prefetched;
}

void OmniIndexer::ConnectBlock(CBlockTx&, const BlockHeader& header)
{
    // Everything that needs the node comes first: a failure here leaves the
    // state untouched and the syncer retries. It runs without cs_main, so
    // the API keeps answering meanwhile.
    std::vector<CTransactionRef> txs = m_node.GetBlockTxs(header.hash);
    std::map<uint256, CTransactionRef> blockTxs;
    for (const CTransactionRef& tx : txs) {
        if (tx) blockTxs[tx->GetHash()] = tx;
    }
    Prefetched prefetched = Prefetch(txs, blockTxs, header.height);

    LOCK(cs_main);
    ClearBlockCaches();
    m_blockTxs.swap(blockTxs);
    m_prefetched.swap(prefetched);
    struct ClearOnExit {
        OmniIndexer& self;
        ~ClearOnExit() { self.ClearBlockCaches(); }
    } clearOnExit{*this};

    CBlockIndex* tip = chainActive.Tip();
    if (tip && tip->GetBlockHash().GetHex() != header.prevHash) {
        throw FatalError("block " + header.hash + " does not extend OmniLite's chain tip " + tip->GetBlockHash().GetHex());
    }
    CBlockIndex* pindex = AddIndex(header.height, header.hash, header.time);
    pindex->pprev = tip;

    // As ConnectTip: begin with the previous height, then the new tip, the
    // transactions and the end of the block.
    try {
        unsigned int nNumMetaTxs = mastercore_handler_block_begin(chainActive.Height(), pindex);
        chainActive.SetTip(pindex);
        for (unsigned int idx = 0; idx < txs.size(); ++idx) {
            if (txs[idx] && mastercore_handler_tx(*txs[idx], pindex->nHeight, idx, pindex, nullptr)) ++nNumMetaTxs;
        }
        mastercore_handler_block_end(pindex->nHeight, pindex, nNumMetaTxs);
    } catch (const std::exception& e) {
        throw FatalError("block " + std::to_string(header.height) + " failed half way, restart to reload the last saved state: " + e.what());
    }
    if (NodeAborted()) throw FatalError("OmniLite stopped at block " + std::to_string(header.height) + ", see the log");
}

void OmniIndexer::DisconnectBlock(const BlockInfo& block)
{
    // As DisconnectTip: the previous block becomes the tip, then OmniLite
    // learns about it and rewinds its state when the next block comes.
    LOCK(cs_main);
    CBlockIndex* tip = chainActive.Tip();
    if (!tip || tip->GetBlockHash().GetHex() != block.hash) {
        throw FatalError("disconnected block " + block.hash + " is not OmniLite's chain tip");
    }
    chainActive.SetTip(tip->pprev);
    mastercore_handler_disc_begin(block.height);
}
