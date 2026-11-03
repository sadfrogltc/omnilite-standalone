// omnilite-standalone: stands in for Litecoin Core's interfaces/wallet.h, with
// the part OmniLite's sending code (rpctx.cpp) uses, over the node's wallet
// (src/nodewallet.cpp). Elsewhere OmniLite passes a null pointer around.

#ifndef OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H
#define OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H

#include <amount.h>
#include <primitives/transaction.h>
#include <script/ismine.h>
#include <script/standard.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class CCoinControl;
class CWallet;
struct CRecipient;

namespace interfaces {

using WalletValueMap = std::map<std::string, std::string>;
using WalletOrderForm = std::vector<std::pair<std::string, std::string>>;

//! Tracking object returned by CreateTransaction and passed to CommitTransaction.
class PendingWalletTx
{
public:
    virtual ~PendingWalletTx() {}

    //! Get transaction data.
    virtual const CTransaction& get() = 0;

    //! Send pending transaction and commit to wallet.
    virtual bool commit(WalletValueMap value_map,
        WalletOrderForm order_form,
        std::string& reject_reason) = 0;
};

class Wallet
{
public:
    explicit Wallet(std::shared_ptr<CWallet> wallet) : m_wallet(std::move(wallet)) {}
    virtual ~Wallet() = default;

    //! Return whether wallet has private key.
    isminetype isMine(const CTxDestination& dest);

    //! Create transaction. Only the inputs selected in coin_control are
    //! used; change goes to coin_control.destChange, before the last output.
    std::unique_ptr<PendingWalletTx> createTransaction(const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        std::string& fail_reason,
        bool omni = false,
        CAmount min_fee = 0);

    //! Not in Core: the node's wallet behind this interface.
    CWallet& wallet() { return *m_wallet; }

private:
    std::shared_ptr<CWallet> m_wallet;
};

//! Return implementation of Wallet interface; null for a null wallet.
std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet);

} // namespace interfaces

#endif // OMNILITE_STANDALONE_CORESHIM_INTERFACES_WALLET_H
