// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_LOGGING_H
#define OMNILITE_STANDALONE_LOGGING_H

#include <tinyformat.h>

#include <string>

enum class LogLevel { Debug, Info, Warn, Error };

/** Writes one line with time and level to stderr. Thread-safe. */
void LogLine(LogLevel level, const std::string& message);

/** Messages below this level are dropped (default: Info). */
void SetLogLevel(LogLevel level);

template <typename... Args>
void LogInfo(const char* fmt, const Args&... args) { LogLine(LogLevel::Info, tfm::format(fmt, args...)); }
template <typename... Args>
void LogWarn(const char* fmt, const Args&... args) { LogLine(LogLevel::Warn, tfm::format(fmt, args...)); }
template <typename... Args>
void LogError(const char* fmt, const Args&... args) { LogLine(LogLevel::Error, tfm::format(fmt, args...)); }
template <typename... Args>
void LogDebug(const char* fmt, const Args&... args) { LogLine(LogLevel::Debug, tfm::format(fmt, args...)); }

#endif // OMNILITE_STANDALONE_LOGGING_H
