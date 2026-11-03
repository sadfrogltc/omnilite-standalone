// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Plain POSIX tar (ustar) for snapshots, without a library: readable with
// any tar. The reader takes regular files with relative paths only; links,
// absolute paths and ".." are refused, so an archive cannot write outside
// the directory it is unpacked into.

#ifndef OMNILITE_STANDALONE_TAR_H
#define OMNILITE_STANDALONE_TAR_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>

class TarError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class TarWriter
{
public:
    explicit TarWriter(const std::string& path);
    /** A file from memory. */
    void Add(const std::string& name, const std::string& data);
    /** A file from disk; returns its SHA-256 (hex). */
    std::string AddFile(const std::string& name, const std::string& source);
    /** Writes the end of the archive and closes it. */
    void Finish();

private:
    void Header(const std::string& name, uint64_t size);
    void Pad(uint64_t size);
    std::ofstream m_out;
    std::string m_path;
};

class TarReader
{
public:
    explicit TarReader(const std::string& path);

    /** The next file; false at the end of the archive. */
    bool Next(std::string& name, uint64_t& size);
    /** Hands the current file's data to sink in pieces; must be called once per file. */
    void Read(const std::function<void(const char*, size_t)>& sink);

private:
    std::ifstream m_in;
    uint64_t m_left = 0;
    bool m_pending = false;
};

/** SHA-256 of a file on disk (hex). */
std::string FileSHA256(const std::string& path);

#endif // OMNILITE_STANDALONE_TAR_H
