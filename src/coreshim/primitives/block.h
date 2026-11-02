// omnilite-standalone: stands in for Litecoin Core's primitives/block.h: a
// block is its transactions here.

#ifndef OMNILITE_STANDALONE_CORESHIM_PRIMITIVES_BLOCK_H
#define OMNILITE_STANDALONE_CORESHIM_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>

#include <vector>

class CBlock
{
public:
    std::vector<CTransactionRef> vtx;
};

#endif // OMNILITE_STANDALONE_CORESHIM_PRIMITIVES_BLOCK_H
