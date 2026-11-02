// omnilite-standalone: stands in for Litecoin Core's util/time.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_UTIL_TIME_H
#define OMNILITE_STANDALONE_CORESHIM_UTIL_TIME_H

#include <cstdint>
#include <string>

int64_t GetTime();
int64_t GetTimeMillis();
int64_t GetTimeMicros();
std::string FormatISO8601DateTime(int64_t nTime);

#endif // OMNILITE_STANDALONE_CORESHIM_UTIL_TIME_H
