// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Snapshots: the index and OmniLite's state of one network at one block, as
// a tar file, so another installation starts there instead of at the Omni
// genesis block. Export only while the program is stopped. Import only with
// the consensus hash given separately: it is computed again from the
// imported state, the one written in the file does not count.

#ifndef OMNILITE_STANDALONE_SNAPSHOT_H
#define OMNILITE_STANDALONE_SNAPSHOT_H

#include <indexer.h>

#include <cstdint>
#include <stdexcept>
#include <string>

class ChainSource;

class SnapshotError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** Name of the description inside the archive, its first entry. */
extern const char* const SNAPSHOT_MANIFEST;

struct SnapshotInfo {
    std::string network;
    int64_t height = 0;
    std::string blockHash;
    int64_t blockTime = 0;
    std::string consensusHash;
    size_t files = 0;
};

/**
 * Writes netDir (<datadir>/<network>: index/ and omnicore/) to file. Loads
 * OmniLite once for the consensus hash, which needs the node only if blocks
 * have to be replayed. Fails if the program is running (the databases are locked).
 */
SnapshotInfo ExportSnapshot(const std::string& netDir, int64_t undoDepth, NodeData& node, const OmniOptions& options,
                            const std::string& file);

/**
 * Unpacks file into netDir, which must not hold data yet. Checks every file's
 * SHA-256, that the snapshot's block is in the node's chain, and that
 * OmniLite's consensus hash of the imported state is expectedHash; on any
 * failure nothing is left behind.
 */
SnapshotInfo ImportSnapshot(const std::string& netDir, int64_t undoDepth, NodeData& node, ChainSource& chain,
                            const OmniOptions& options, const std::string& file, const std::string& expectedHash);

#endif // OMNILITE_STANDALONE_SNAPSHOT_H
