// omnilite-standalone: stands in for Litecoin Core's fs.h, with std::filesystem
// instead of boost::filesystem.

#ifndef OMNILITE_STANDALONE_CORESHIM_FS_H
#define OMNILITE_STANDALONE_CORESHIM_FS_H

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace fsbridge {
using ifstream = std::ifstream;
using ofstream = std::ofstream;
inline std::string get_filesystem_error_message(const fs::filesystem_error& e) { return e.what(); }
} // namespace fsbridge

#endif // OMNILITE_STANDALONE_CORESHIM_FS_H
