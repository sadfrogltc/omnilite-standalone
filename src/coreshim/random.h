// omnilite-standalone: stands in for Litecoin Core's random.h, for the RPC
// cookie (rpc/protocol.cpp).

#ifndef OMNILITE_STANDALONE_CORESHIM_RANDOM_H
#define OMNILITE_STANDALONE_CORESHIM_RANDOM_H

/** Fills buf with bytes from the operating system's random source. */
void GetRandBytes(unsigned char* buf, int num);

#endif // OMNILITE_STANDALONE_CORESHIM_RANDOM_H
