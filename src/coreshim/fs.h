// omnilite-standalone: stands in for Litecoin Core's fs.h, with std::filesystem
// instead of boost::filesystem.

#ifndef OMNILITE_STANDALONE_CORESHIM_FS_H
#define OMNILITE_STANDALONE_CORESHIM_FS_H

#include <filesystem>

namespace fs = std::filesystem;

#endif // OMNILITE_STANDALONE_CORESHIM_FS_H
