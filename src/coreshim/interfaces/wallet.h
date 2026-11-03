// omnilite-standalone: stands in for Litecoin Core's interfaces/wallet.h.
// There is no wallet; OmniLite only passes a null pointer around (and holds
// one in a std::unique_ptr, which needs a complete type).

#ifndef OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H
#define OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H

namespace interfaces {
class Wallet
{
public:
    virtual ~Wallet() = default;
};
} // namespace interfaces

#endif // OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H
