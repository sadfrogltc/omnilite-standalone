// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_INDEXER_H
#define OMNILITE_STANDALONE_INDEXER_H

// Feeds the blocks the syncer connects into OmniLite's handlers, in the
// order Litecoin Core calls them (validation.cpp, ConnectTip and
// DisconnectTip). OmniLite keeps the Omni state itself, in memory, in its
// databases and in its state files, and handles reorgs on its own.

#include <fs.h>
#include <primitives/transaction.h>
#include <syncer.h>
#include <uint256.h>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class CBlockIndex;
class RPCClient;

/** Decodes a transaction from hex as Litecoin Core 0.18 serializes it; throws on failure. */
CTransactionRef DecodeTx(const std::string& hex);

/** What the indexer needs from the node. Failures to reach it throw NodeError. */
class NodeData
{
public:
    virtual ~NodeData() = default;
    /** The transactions of a block; nullptr for those Litecoin 0.18 cannot read (MWEB). */
    virtual std::vector<CTransactionRef> GetBlockTxs(const std::string& hash) = 0;
    /** A transaction and the block it is in; false if the node does not know it. */
    virtual bool GetTx(const uint256& txid, CTransactionRef& tx, uint256& blockHash) = 0;
};

/** NodeData over RPC: getblock (verbosity 2) and getrawtransaction (needs -txindex). */
class RPCNodeData : public NodeData
{
public:
    explicit RPCNodeData(const RPCClient& node) : m_node(node) {}
    std::vector<CTransactionRef> GetBlockTxs(const std::string& hash) override;
    bool GetTx(const uint256& txid, CTransactionRef& tx, uint256& blockHash) override;

private:
    const RPCClient& m_node;
};

/** The settings OmniLite reads as -omni* arguments. */
struct OmniOptions {
    std::vector<std::string> activationAllowSenders;
    std::vector<std::string> alertAllowSenders;
    /** -omnidebug categories. */
    std::vector<std::string> debug;
};

class OmniIndexer : public Processor
{
public:
    OmniIndexer(NodeData& node, CStore& store);
    ~OmniIndexer() override;

    /**
     * Rebuilds the chain index from the store and starts OmniLite
     * (mastercore_init), which loads its saved state from dataDir and catches
     * up to the store's tip.
     */
    void Start(const fs::path& dataDir, const OmniOptions& options);

    void ConnectBlock(CBlockTx& tx, const BlockHeader& header) override;
    void DisconnectBlock(const BlockInfo& block) override;

private:
    CBlockIndex* AddIndex(int64_t height, const std::string& hash, int64_t time);
    /** Fetches the inputs of the transactions with an Omni marker. */
    void Prefetch(const std::vector<CTransactionRef>& txs, int64_t height);
    bool GetTransaction(const uint256& txid, CTransactionRef& tx, uint256& blockHash);
    bool ReadBlock(const CBlockIndex* pindex, std::vector<CTransactionRef>& txs);

    NodeData& m_node;
    CStore& m_store;
    bool m_started = false;
    std::list<CBlockIndex> m_index;
    /** Transactions of the block being processed and inputs fetched for it. */
    std::map<uint256, CTransactionRef> m_blockTxs;
    std::map<uint256, std::pair<CTransactionRef, uint256>> m_prefetched;
};

#endif // OMNILITE_STANDALONE_INDEXER_H
