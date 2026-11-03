// omnilite-standalone: stands in for Litecoin Core's shutdown.h.

#ifndef OMNILITE_STANDALONE_CORESHIM_SHUTDOWN_H
#define OMNILITE_STANDALONE_CORESHIM_SHUTDOWN_H

#include <atomic>

void StartShutdown();
bool ShutdownRequested();

/** Not in Core: the flag itself, for Syncer::Run. Set by signals, the stop RPC and DoAbortNode. */
const std::atomic<bool>& ShutdownFlag();

#endif // OMNILITE_STANDALONE_CORESHIM_SHUTDOWN_H
