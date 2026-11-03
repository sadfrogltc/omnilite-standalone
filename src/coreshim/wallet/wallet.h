// omnilite-standalone: stands in for Litecoin Core's wallet/wallet.h. A
// CWallet here is a wallet of the node: its name and the RPC client that
// reaches it. Everything is asked from the node (src/nodewallet.cpp).

#ifndef OMNILITE_STANDALONE_CORESHIM_WALLET_WALLET_H
#define OMNILITE_STANDALONE_CORESHIM_WALLET_WALLET_H

#include <amount.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <rpcclient.h>
#include <script/ismine.h>
#include <script/script.h>
#include <script/standard.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class JSONRPCRequest;

struct CRecipient
{
    CScript scriptPubKey;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
};

/** A spendable output of the node's wallet. */
struct NodeCoin {
    COutPoint outpoint;
    CTxOut out;
    std::string address;
};

class CWallet
{
public:
    CWallet(std::string name, RPCClient client) : m_name(std::move(name)), m_client(std::move(client)) {}

    const std::string& GetName() const { return m_name; }

    /**
     * A call to the node's wallet. Errors of the node become JSONRPCError
     * (thrown UniValue), so the API caller sees the node's own message,
     * e.g. that the wallet is locked.
     */
    UniValue Call(const std::string& method, const UniValue& params = UniValue(UniValue::VARR)) const;

    /** Spendable, trusted outputs of an address (listunspent), in the node's order. */
    std::vector<NodeCoin> ListCoins(const std::string& address) const;
    /**
     * Whether the node's wallet has the key of the destination (getaddressinfo),
     * asked once per address and request. ISMINE_NO where the node has no such
     * wallet: OmniLite shows "ismine" in every transaction object, those calls
     * must not fail over it.
     */
    isminetype IsMine(const CTxDestination& dest) const;
    /**
     * The wallet's addresses, as its address book in Core (listreceivedbyaddress
     * with empty ones): spendable ones, and watch-only ones if asked.
     */
    std::vector<std::string> ListAddresses(bool includeWatchOnly) const;
    /** A transaction of the node's wallet; blockHash null while unconfirmed. */
    struct Tx {
        uint256 txid;
        uint256 blockHash;
    };
    /** The wallet's transactions, oldest first (listtransactions, watch-only included). */
    std::vector<Tx> ListTransactions() const;
    /** The public key of a wallet address; false if the wallet has none. */
    bool GetPubKey(const std::string& address, CPubKey& pubKey) const;
    /**
     * Fee rate as the node's wallet would pay: its paytxfee if set, else the
     * node's estimate (estimatesmartfee, 6 blocks), else 0.0001 LTC/kB; never
     * below the node's relay or mempool minimum.
     */
    CFeeRate GetFeeRate() const;
    /** Signs with the node's wallet; throws unless every input is signed. */
    CMutableTransaction Sign(const CMutableTransaction& tx) const;
    /** Broadcasts; false with the node's reason if it is rejected. */
    bool Send(const CTransaction& tx, std::string& rejectReason) const;

    /** Outputs seen by ListCoins, for createTransaction. */
    mutable std::map<COutPoint, NodeCoin> m_coins;

private:
    /** GetFeeRate, asked once per request. */
    mutable CAmount m_feePerKb = -1;
    /** IsMine by address, for this request. */
    mutable std::map<std::string, isminetype> m_isMine;
    std::string m_name;
    RPCClient m_client;
};

/** The node to send through; null: no wallet (as -disablewallet). Set at start-up. */
void SetWalletNode(const RPCClient* node);

/** Whether there is a node to ask for wallets. */
bool HasWallets();

/**
 * The wallet a request is for: the one in its URI (/wallet/<name>, as
 * litecoin-cli -rpcwallet), else the node's default. Throws
 * RPC_METHOD_NOT_FOUND without a node, as Core without wallets.
 */
std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest& request);

#endif // OMNILITE_STANDALONE_CORESHIM_WALLET_WALLET_H
