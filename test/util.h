// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_TEST_UTIL_H
#define OMNILITE_STANDALONE_TEST_UTIL_H

#include <cstdlib>
#include <filesystem>
#include <string>

/** A temporary directory, removed with everything in it at the end. */
struct TempDir {
    std::string path;
    TempDir()
    {
        char tmpl[] = "/tmp/omnilite-test-XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string Sub(const std::string& name) const { return path + "/" + name; }
};

#endif // OMNILITE_STANDALONE_TEST_UTIL_H
