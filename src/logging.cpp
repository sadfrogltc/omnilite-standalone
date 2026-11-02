// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
std::mutex logMutex;
std::atomic<LogLevel> minLevel{LogLevel::Info};
} // namespace

void SetLogLevel(LogLevel level) { minLevel = level; }

void LogLine(LogLevel level, const std::string& message)
{
    if (level < minLevel) return;
    static const char* const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::time_t now = std::time(nullptr);
    std::tm tm;
    gmtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    std::lock_guard<std::mutex> lock(logMutex);
    std::fprintf(stderr, "%s %-5s %s\n", stamp, names[static_cast<int>(level)], message.c_str());
}
