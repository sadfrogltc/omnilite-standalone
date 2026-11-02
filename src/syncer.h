// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_SYNCER_H
#define OMNILITE_STANDALONE_SYNCER_H

// Follows the node's best chain block by block, handing each block to a
// Processor and disconnecting blocks again on reorgs.

#include <store.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

class RPCClient;

struct BlockHeader {
    std::string hash;
    int64_t height = 0;
    std::string prevHash; // empty for the genesis block
    int64_t time = 0;
};

/** A request to the node failed; Run retries these. */
class NodeError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** The node calls the syncer needs. Implementations throw NodeError. */
class ChainSource
{
public:
    virtual ~ChainSource() = default;
    virtual int64_t GetBlockCount() = 0;
    /** The hash at height; throws HeightOutOfRange if the chain is shorter. */
    virtual std::string GetBlockHash(int64_t height) = 0;
    virtual BlockHeader GetBlockHeader(const std::string& hash) = 0;
};

/** GetBlockHash was asked for a height above the node's tip. */
class HeightOutOfRange : public NodeError
{
public:
    using NodeError::NodeError;
};

/** ChainSource over RPC. Unauthorized logins are not retried. */
class RPCChainSource : public ChainSource
{
public:
    explicit RPCChainSource(const RPCClient& node) : m_node(node) {}
    int64_t GetBlockCount() override;
    std::string GetBlockHash(int64_t height) override;
    BlockHeader GetBlockHeader(const std::string& hash) override;

private:
    const RPCClient& m_node;
};

/** Applies one block to the state; all writes go through tx, so they are undone on reorgs. */
class Processor
{
public:
    virtual ~Processor() = default;
    virtual void ConnectBlock(CBlockTx& tx, const BlockHeader& header) = 0;
};

/** Records the blocks only. */
class NopProcessor : public Processor
{
public:
    void ConnectBlock(CBlockTx&, const BlockHeader&) override {}
};

class Syncer
{
public:
    Syncer(ChainSource& source, CStore& store, Processor& processor, int64_t startHeight,
        std::chrono::milliseconds poll);

    /** Connects or disconnects at most one block; false once the store matches the node's tip. */
    bool Step();

    /** Syncs until stop is set. Node errors are logged and retried; others end the run. */
    void Run(const std::atomic<bool>& stop);

private:
    void Connect(const BlockHeader& header);
    void Disconnect();

    ChainSource& m_source;
    CStore& m_store;
    Processor& m_processor;
    int64_t m_start;
    std::chrono::milliseconds m_poll;
    int64_t m_nodeHeight = -1;
    std::chrono::steady_clock::time_point m_lastLog{};
};

#endif // OMNILITE_STANDALONE_SYNCER_H
