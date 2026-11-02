// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_STORE_H
#define OMNILITE_STANDALONE_STORE_H

// Persists the processed chain and the Omni state in LevelDB.
//
// Every block is written in one atomic batch. For each state key a block
// changes, the value before the block is kept as undo data, so the tip
// block can be disconnected again during a reorg. Undo data older than the
// configured depth is pruned.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace leveldb {
class DB;
}

class StoreError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** Begin was given a block that does not extend the tip. */
class NotOnTipError : public StoreError
{
public:
    using StoreError::StoreError;
};

/** A reorg goes deeper than the kept undo data; the database has to be rebuilt. */
class UndoUnavailableError : public StoreError
{
public:
    UndoUnavailableError();
};

struct BlockInfo {
    int64_t height = 0;
    std::string hash;
    std::string prevHash;
    int64_t time = 0;
};

using KeyValues = std::vector<std::pair<std::string, std::string>>;

/** Read access to the state: values by namespace and key. */
class KVReader
{
public:
    virtual ~KVReader() = default;
    /** Reads a value; false if the key does not exist. */
    virtual bool Get(const std::string& ns, const std::string& key, std::string& value) const = 0;
    /** All entries of ns whose key starts with prefix, ordered by key (byte order). */
    virtual KeyValues Scan(const std::string& ns, const std::string& prefix) const = 0;
};

/** Read and write access, as the state logic of one block has it. */
class KVWriter : public KVReader
{
public:
    virtual void Put(const std::string& ns, const std::string& key, const std::string& value) = 0;
    virtual void Delete(const std::string& ns, const std::string& key) = 0;
};

class CStore;

/** The state changes of one block, written by Commit. */
class CBlockTx : public KVWriter
{
public:
    CBlockTx(CStore& store, BlockInfo block);

    const BlockInfo& Block() const { return m_block; }

    bool Get(const std::string& ns, const std::string& key, std::string& value) const override;
    KeyValues Scan(const std::string& ns, const std::string& prefix) const override;
    void Put(const std::string& ns, const std::string& key, const std::string& value) override;
    void Delete(const std::string& ns, const std::string& key) override;

    /** Writes the block, its changes and its undo data in one batch. */
    void Commit();

private:
    void Change(const std::string& ns, const std::string& key, std::optional<std::string> value);

    CStore& m_store;
    BlockInfo m_block;
    bool m_committed = false;
    /** New values by full database key; nullopt marks a deletion. */
    std::map<std::string, std::optional<std::string>> m_changes;
    /** Values before the block, by full database key; nullopt if absent. */
    std::map<std::string, std::optional<std::string>> m_undo;
};

class CStore : public KVReader
{
public:
    /**
     * Opens or creates the database in directory path. network is stored on
     * first use and checked afterwards, so a mainnet database is never fed
     * testnet blocks.
     */
    CStore(const std::string& path, const std::string& network, int64_t undoDepth);
    ~CStore();
    CStore(const CStore&) = delete;
    CStore& operator=(const CStore&) = delete;

    /** The last processed block, if any. */
    std::optional<BlockInfo> Tip() const;
    /** The processed block at height, if any. */
    std::optional<BlockInfo> BlockAt(int64_t height) const;

    bool Get(const std::string& ns, const std::string& key, std::string& value) const override;
    KeyValues Scan(const std::string& ns, const std::string& prefix) const override;

    /** Starts writing block, which must extend the current tip. */
    std::unique_ptr<CBlockTx> Begin(const BlockInfo& block);

    /** Removes the tip block and reverts its state changes; returns the removed block. */
    BlockInfo Disconnect();

private:
    friend class CBlockTx;

    bool GetRaw(const std::string& key, std::string& value) const;
    KeyValues ScanRaw(const std::string& prefix) const;
    /** Keys starting with prefix that sort before end. */
    std::vector<std::string> KeysBefore(const std::string& prefix, const std::string& end) const;
    std::string GetMeta(const std::string& name) const;

    std::unique_ptr<leveldb::DB> m_db;
    int64_t m_undoDepth;
};

/** Database key of a state entry: 'S' + ns + '\0' + key. */
std::string StateKey(const std::string& ns, const std::string& key);

#endif // OMNILITE_STANDALONE_STORE_H
