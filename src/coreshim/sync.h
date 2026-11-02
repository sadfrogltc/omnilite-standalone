// omnilite-standalone: stands in for Litecoin Core's sync.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_SYNC_H
#define OMNILITE_STANDALONE_CORESHIM_SYNC_H

#include <mutex>

typedef std::recursive_mutex CCriticalSection;

#define OMNI_PASTE(x, y) x##y
#define OMNI_PASTE2(x, y) OMNI_PASTE(x, y)
#define OMNI_UNIQUE(name) OMNI_PASTE2(name, __COUNTER__)

#define LOCK(cs) std::lock_guard<std::recursive_mutex> OMNI_UNIQUE(omni_lock_)(cs)
#define LOCK2(cs1, cs2) std::scoped_lock<std::recursive_mutex, std::recursive_mutex> OMNI_UNIQUE(omni_lock2_)(cs1, cs2)
#define AssertLockHeld(cs)
#define AssertLockNotHeld(cs)

#endif // OMNILITE_STANDALONE_CORESHIM_SYNC_H
