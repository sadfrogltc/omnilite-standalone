// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <config.h>

#include <chainparams.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

const char* const CONFIG_FILENAME = "omnilite-standalone.conf";

namespace {

/** Keys allowed in the config file and as -key=value. */
const std::set<std::string> SETTINGS = {
    "network", "rpcconnect", "rpcport", "rpcuser", "rpcpassword", "rpccookiefile",
    "pollinterval", "undodepth",
};

std::string Trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string HomeDir()
{
    const char* home = std::getenv("HOME");
    return home && *home ? home : ".";
}

std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
}

int ParseInt(const std::string& key, const std::string& value, int min)
{
    char* end = nullptr;
    errno = 0;
    long n = std::strtol(value.c_str(), &end, 10);
    if (value.empty() || *end != '\0' || errno != 0 || n < min || n > 1000000000) {
        throw ConfigError(key + ": want a whole number >= " + std::to_string(min) + ", got \"" + value + "\"");
    }
    return static_cast<int>(n);
}

/** Reads key=value lines. Returns false if the file does not exist. */
bool ReadConfigFile(const std::string& path, std::map<std::string, std::string>& values)
{
    std::ifstream file(path);
    if (!file) return false;
    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            throw ConfigError(path + ":" + std::to_string(lineNo) + ": expected key=value");
        }
        std::string key = Trim(line.substr(0, eq));
        if (!SETTINGS.count(key)) {
            throw ConfigError(path + ":" + std::to_string(lineNo) + ": unknown setting \"" + key + "\"");
        }
        values[key] = Trim(line.substr(eq + 1));
    }
    return true;
}

} // namespace

std::string Config::RPCURL() const
{
    return "http://" + rpcConnect + ":" + std::to_string(rpcPort);
}

std::string Config::DBPath() const
{
    return JoinPath(JoinPath(dataDir, Params().NetworkIDString()), "index");
}

std::pair<std::string, std::string> Config::Credentials() const
{
    if (!rpcPassword.empty()) return {rpcUser, rpcPassword};
    std::string path = rpcCookieFile.empty() ? DefaultCookieFile() : rpcCookieFile;
    std::ifstream file(path);
    std::string cookie;
    if (!file || !std::getline(file, cookie)) {
        throw ConfigError("no rpcpassword set and cookie file " + path + " is not readable");
    }
    cookie = Trim(cookie);
    size_t colon = cookie.find(':');
    if (colon == std::string::npos) {
        throw ConfigError("cookie file " + path + " has no user:password");
    }
    return {cookie.substr(0, colon), cookie.substr(colon + 1)};
}

std::string DefaultDataDir()
{
    return JoinPath(HomeDir(), ".omnilite-standalone");
}

std::string DefaultCookieFile()
{
    std::string dir = JoinPath(HomeDir(), ".litecoin");
    if (!Params().DataDirSuffix().empty()) dir = JoinPath(dir, Params().DataDirSuffix());
    return JoinPath(dir, ".cookie");
}

Config LoadConfig(const std::vector<std::string>& args)
{
    Config cfg;
    cfg.dataDir = DefaultDataDir();
    std::string confPath;
    std::map<std::string, std::string> flags;

    for (std::string arg : args) {
        if (arg.rfind("--", 0) == 0) arg.erase(0, 1);
        if (arg.empty() || arg[0] != '-') {
            throw ConfigError("unexpected argument \"" + arg + "\"");
        }
        arg.erase(0, 1);
        size_t eq = arg.find('=');
        std::string key = arg.substr(0, eq);
        std::string value = eq == std::string::npos ? "" : arg.substr(eq + 1);
        if (key == "help" || key == "h" || key == "?") {
            cfg.help = true;
        } else if (key == "check") {
            cfg.check = true;
        } else if (key == "datadir") {
            cfg.dataDir = value;
        } else if (key == "conf") {
            confPath = value;
        } else if (SETTINGS.count(key)) {
            flags[key] = value;
        } else {
            throw ConfigError("unknown option -" + key + " (see -help)");
        }
    }
    if (cfg.dataDir.empty()) throw ConfigError("-datadir must not be empty");

    std::map<std::string, std::string> values;
    std::string path = confPath.empty() ? JoinPath(cfg.dataDir, CONFIG_FILENAME) : confPath;
    if (!ReadConfigFile(path, values) && !confPath.empty()) {
        throw ConfigError("config file " + path + " not found");
    }
    for (const auto& kv : flags) values[kv.first] = kv.second;

    cfg.network = values.count("network") ? values["network"] : "main";
    try {
        SelectParams(cfg.network);
    } catch (const std::runtime_error& e) {
        throw ConfigError(e.what());
    }
    cfg.rpcPort = Params().RPCPort();
    if (values.count("rpcconnect") && !values["rpcconnect"].empty()) cfg.rpcConnect = values["rpcconnect"];
    if (values.count("rpcport")) cfg.rpcPort = ParseInt("rpcport", values["rpcport"], 1);
    cfg.rpcUser = values["rpcuser"];
    cfg.rpcPassword = values["rpcpassword"];
    cfg.rpcCookieFile = values["rpccookiefile"];
    if (values.count("pollinterval")) cfg.pollInterval = ParseInt("pollinterval", values["pollinterval"], 1);
    if (values.count("undodepth")) cfg.undoDepth = ParseInt("undodepth", values["undodepth"], 10);
    return cfg;
}

std::string HelpMessage()
{
    std::ostringstream s;
    s << "Usage: omnilite-standalone [options]\n\n"
      << "Indexes OmniLite transactions from any litecoind over standard RPC.\n\n"
      << "Options:\n"
      << "  -check              check the node connection and exit\n"
      << "  -datadir=<dir>      data directory (default: " << DefaultDataDir() << ")\n"
      << "  -conf=<file>        config file (default: <datadir>/" << CONFIG_FILENAME << ")\n"
      << "  -network=<net>      main, test or regtest (default: main)\n"
      << "  -rpcconnect=<host>  litecoind RPC host (default: 127.0.0.1)\n"
      << "  -rpcport=<port>     litecoind RPC port (default: 9332, test 19332, regtest 19443)\n"
      << "  -rpcuser=<user>     litecoind RPC user\n"
      << "  -rpcpassword=<pw>   litecoind RPC password (default: use the cookie file)\n"
      << "  -rpccookiefile=<f>  litecoind cookie file (default: ~/.litecoin/[net/].cookie)\n"
      << "  -pollinterval=<s>   seconds between checks for new blocks (default: 5)\n"
      << "  -undodepth=<n>      blocks of undo data kept for reorgs (default: 100)\n"
      << "\nEvery option except -check, -datadir and -conf can also be set in the config file.\n";
    return s.str();
}
