// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <indexer.h>

#include <logging.h>
#include <omnicore/parsing.h>
#include <omnicore/tx.h>
#include <rpcclient.h>
#include <streams.h>
#include <util/strencodings.h>
#include <version.h>

const std::string NS_TX = "tx";
const std::string NS_BLOCKTX = "blocktx";
const std::string NS_COUNT = "count";

namespace {

/** Fetched transactions kept for input lookups before the cache is cleared. */
const size_t INPUT_CACHE_SIZE = 50000;

template <typename T>
std::string Serialize(const T& obj)
{
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << obj;
    return ss.str();
}

template <typename T>
T Deserialize(const std::string& data)
{
    CDataStream ss(data.data(), data.data() + data.size(), SER_DISK, PROTOCOL_VERSION);
    T obj;
    ss >> obj;
    return obj;
}

} // namespace

std::string BlockTxKey(int64_t height, unsigned int index)
{
    return strprintf("%010d:%05d", height, index);
}

std::optional<OmniTxRecord> GetOmniTx(const KVReader& kv, const std::string& txid)
{
    std::string v;
    if (!kv.Get(NS_TX, txid, v)) return std::nullopt;
    return Deserialize<OmniTxRecord>(v);
}

int64_t CountOmniTxs(const KVReader& kv)
{
    std::string v;
    return kv.Get(NS_COUNT, "total", v) ? std::stoll(v) : 0;
}

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

// --- RPCInputSource ---

bool RPCInputSource::GetPrevout(const COutPoint& prevout, CTxOut& out)
{
    if (prevout.IsNull()) return false; // coinbase
    CTransactionRef tx;
    if (m_block) {
        auto it = m_block->find(prevout.hash);
        if (it != m_block->end()) tx = it->second;
    }
    if (!tx) {
        auto it = m_cache.find(prevout.hash);
        if (it != m_cache.end()) {
            tx = it->second;
        } else {
            UniValue params(UniValue::VARR);
            params.push_back(prevout.hash.GetHex());
            params.push_back(false);
            try {
                tx = DecodeTx(m_node.Call("getrawtransaction", params).get_str());
            } catch (const RPCError& e) {
                if (e.code == RPC_INVALID_ADDRESS_OR_KEY) return false; // not found
                throw NodeError("getrawtransaction " + prevout.hash.GetHex() + ": " + e.what());
            } catch (const NodeUnreachable& e) {
                throw NodeError(e.what());
            }
            if (m_cache.size() >= INPUT_CACHE_SIZE) m_cache.clear();
            m_cache[prevout.hash] = tx;
        }
    }
    if (prevout.n >= tx->vout.size()) return false;
    out = tx->vout[prevout.n];
    return true;
}

// --- Indexer ---

Indexer::Indexer(const RPCClient& node) : m_node(node), m_inputs(node) {}

void Indexer::ConnectBlock(CBlockTx& tx, const BlockHeader& header)
{
    UniValue params(UniValue::VARR);
    params.push_back(header.hash);
    params.push_back(2);
    UniValue block;
    try {
        block = m_node.Call("getblock", params);
    } catch (const RPCError& e) {
        throw NodeError("getblock " + header.hash + ": " + e.what());
    } catch (const NodeUnreachable& e) {
        throw NodeError(e.what());
    }
    if (find_value(block, "hash").get_str() != header.hash || find_value(block, "height").get_int64() != header.height) {
        throw std::runtime_error("node returned another block for " + header.hash);
    }

    std::vector<CTransactionRef> txs;
    std::map<uint256, CTransactionRef> byHash;
    const UniValue blockTxs = find_value(block, "tx");
    for (const UniValue& t : blockTxs.getValues()) {
        const std::string txid = find_value(t, "txid").get_str();
        CTransactionRef ptx;
        try {
            ptx = DecodeTx(find_value(t, "hex").get_str());
        } catch (const std::exception& e) {
            // Transactions with MWEB data (Litecoin 0.21) do not exist for
            // OmniLite 0.18, so they cannot carry Omni transactions there.
            LogWarn("block %d: transaction %s skipped, not readable as Litecoin 0.18 (%s)", header.height, txid, e.what());
            txs.push_back(nullptr);
            continue;
        }
        if (ptx->GetHash().GetHex() != txid) {
            throw std::runtime_error("block " + std::to_string(header.height) + ": transaction " + txid + " decodes to " + ptx->GetHash().GetHex());
        }
        byHash[ptx->GetHash()] = ptx;
        txs.push_back(ptx);
    }

    m_inputs.SetBlockTxs(&byHash);
    try {
        ProcessBlockTxs(tx, header.height, header.time, txs, m_inputs);
    } catch (...) {
        m_inputs.SetBlockTxs(nullptr);
        throw;
    }
    m_inputs.SetBlockTxs(nullptr);
}

void ProcessBlockTxs(CBlockTx& tx, int64_t height, int64_t time, const std::vector<CTransactionRef>& txs, InputSource& inputs)
{
    int64_t count = CountOmniTxs(tx);
    int64_t found = 0;
    for (unsigned int idx = 0; idx < txs.size(); ++idx) {
        const CTransactionRef& ptx = txs[idx];
        if (!ptx || ptx->IsCoinBase()) continue;

        // As mastercore_handler_tx: parse, and record only structurally
        // valid transactions. DEx payments (result 1) are recorded once they
        // buy tokens, which needs the state (phase 4).
        CMPTransaction mp;
        int ret = ParseTransaction(*ptx, static_cast<int>(height), idx, mp, inputs, static_cast<unsigned int>(time));
        if (ret != 0) {
            if (ret < -1 && ret != -5) LogDebug("tx %s not parsed: %d", ptx->GetHash().GetHex(), ret);
            continue;
        }
        if (!mp.interpret_Transaction()) {
            LogDebug("tx %s: structurally invalid payload", ptx->GetHash().GetHex());
            continue;
        }

        OmniTxRecord rec;
        rec.txid = ptx->GetHash().GetHex();
        rec.block = height;
        rec.index = idx;
        rec.time = time;
        rec.encodingClass = mp.getEncodingClass();
        rec.sender = mp.getSender();
        rec.reference = mp.getReceiver();
        rec.fee = static_cast<int64_t>(mp.getFeePaid());
        rec.version = mp.getVersion();
        rec.type = static_cast<uint16_t>(mp.getType());
        rec.payload = mp.getPayload();
        tx.Put(NS_TX, rec.txid, Serialize(rec));
        tx.Put(NS_BLOCKTX, BlockTxKey(height, idx), rec.txid);
        ++found;
    }
    if (found > 0) {
        tx.Put(NS_COUNT, "total", std::to_string(count + found));
        LogInfo("block %d: %d Omni transaction(s)", height, found);
    }
}
