// omnilite-standalone: stands in for Litecoin Core's util/system.h: the
// -omni* arguments OmniLite reads and the data directory.

#ifndef OMNILITE_STANDALONE_CORESHIM_UTIL_SYSTEM_H
#define OMNILITE_STANDALONE_CORESHIM_UTIL_SYSTEM_H

#include <fs.h>
#include <logging.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ArgsManager
{
public:
    bool IsArgSet(const std::string& arg) const { return m_args.count(arg) > 0; }
    std::vector<std::string> GetArgs(const std::string& arg) const;
    std::string GetArg(const std::string& arg, const std::string& def) const;
    int64_t GetArg(const std::string& arg, int64_t def) const;
    bool GetBoolArg(const std::string& arg, bool def) const;
    void ForceSetArg(const std::string& arg, const std::string& value) { m_args[arg] = {value}; }
    void ForceSetMultiArg(const std::string& arg, const std::vector<std::string>& values) { m_args[arg] = values; }
    //! Core test helper
    void ForceSetArgs(const std::string& arg, const std::vector<std::string>& values) { m_args[arg] = values; }
    void ClearArgs() { m_args.clear(); }

private:
    std::map<std::string, std::vector<std::string>> m_args;
};

extern ArgsManager gArgs;

/** OmniLite's data directory (state files, databases). */
const fs::path& GetDataDir(bool fNetSpecific = true);
void SetDataDir(const fs::path& path);

/** Creates the directory and its parents; false if it already existed. */
bool TryCreateDirectories(const fs::path& p);

/** A path from a setting: relative ones are taken from the data directory. */
fs::path AbsPathForConfigVal(const fs::path& path, bool net_specific = true);

/** Renames src to dest, replacing dest. */
bool RenameOver(fs::path src, fs::path dest);

/** When the program started, for the uptime RPC. */
int64_t GetStartupTime();

/** Core's LogPrintf: to the program log at info level. */
template <typename... Args>
void LogPrintf(const char* fmt, const Args&... args)
{
    std::string s = tfm::format(fmt, args...);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    LogLine(LogLevel::Info, s);
}

#endif // OMNILITE_STANDALONE_CORESHIM_UTIL_SYSTEM_H
