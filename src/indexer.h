// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_INDEXER_H
#define OMNILITE_STANDALONE_INDEXER_H

// Turns blocks into Omni transaction records. It is the syncer's Processor:
// everything it writes goes through the block's transaction, so reorgs undo it.

#include <omnicore/omnicore.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <syncer.h>
#include <uint256.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class RPCClient;

// State namespaces of the indexer.
/** txid (hex) -> OmniTxRecord */
extern const std::string NS_TX;
/** BlockTxKey(height, index) -> txid, the processing order */
extern const std::string NS_BLOCKTX;
/** "total" -> number of recorded transactions */
extern const std::string NS_COUNT;

/** Key of a transaction in NS_BLOCKTX: height and position, zero-padded. */
std::string BlockTxKey(int64_t height, unsigned int index);

/** A decoded, structurally valid Omni transaction (what OmniLite records via recordTX). */
struct OmniTxRecord {
    std::string txid;
    int64_t block = 0;
    uint32_t index = 0;
    int64_t time = 0;
    int encodingClass = 0;
    std::string sender;
    std::string reference;
    int64_t fee = 0;
    uint16_t version = 0;
    uint16_t type = 0;
    /** The raw payload as hex, interpreted again where needed. */
    std::string payload;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(txid);
        READWRITE(block);
        READWRITE(index);
        READWRITE(time);
        READWRITE(encodingClass);
        READWRITE(sender);
        READWRITE(reference);
        READWRITE(fee);
        READWRITE(version);
        READWRITE(type);
        READWRITE(payload);
    }
};

/** Reads a recorded transaction. */
std::optional<OmniTxRecord> GetOmniTx(const KVReader& kv, const std::string& txid);

/** Number of recorded transactions. */
int64_t CountOmniTxs(const KVReader& kv);

/** Decodes a transaction from hex as Litecoin Core 0.18 serializes it; throws on failure. */
CTransactionRef DecodeTx(const std::string& hex);

/** Looks up spent outputs with getrawtransaction (needs -txindex) and keeps the fetched transactions. */
class RPCInputSource : public InputSource
{
public:
    explicit RPCInputSource(const RPCClient& node) : m_node(node) {}
    bool GetPrevout(const COutPoint& prevout, CTxOut& out) override;
    /** Transactions of the block being processed, found without asking the node. */
    void SetBlockTxs(const std::map<uint256, CTransactionRef>* txs) { m_block = txs; }

private:
    const RPCClient& m_node;
    const std::map<uint256, CTransactionRef>* m_block = nullptr;
    std::map<uint256, CTransactionRef> m_cache;
};

/**
 * Records the Omni transactions of one block, as mastercore_handler_tx does
 * for each transaction. txs may contain nullptr for unreadable transactions.
 */
void ProcessBlockTxs(CBlockTx& tx, int64_t height, int64_t time, const std::vector<CTransactionRef>& txs, InputSource& inputs);

/** Reads blocks with getblock (verbosity 2) and records their Omni transactions. */
class Indexer : public Processor
{
public:
    explicit Indexer(const RPCClient& node);
    void ConnectBlock(CBlockTx& tx, const BlockHeader& header) override;

private:
    const RPCClient& m_node;
    RPCInputSource m_inputs;
};

#endif // OMNILITE_STANDALONE_INDEXER_H
