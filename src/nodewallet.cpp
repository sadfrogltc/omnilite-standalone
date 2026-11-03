// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The node's wallet for OmniLite: sending, and the wallet calls that only read
// (omni_listtransactions, omni_getwallet*balances, "ismine"). OmniLite's
// calls (rpctx.cpp, rpc.cpp) stay as they are; what they ask of Litecoin Core's wallet is answered here over
// RPC: the stand-ins of wallet/wallet.h, wallet/coincontrol.h and
// interfaces/wallet.h, OmniLite's coin selection (walletutils.cpp) and its
// transaction builder (wallettxbuilder.cpp). Inputs only ever come from the
// sending address, change goes back to it, the node's wallet signs and
// broadcasts. Keys never leave the node.

#include <wallet/wallet.h>

#include <chain.h>
#include <core_io.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <logging.h>
#include <omnicore/encoding.h>
#include <omnicore/errors.h>
#include <omnicore/log.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/script.h>
#include <omnicore/wallettxbuilder.h>
#include <omnicore/walletutils.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <tinyformat.h>
#include <txmempool.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <validation.h>
#include <version.h>
#include <wallet/coincontrol.h>

#include <event2/http.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <set>

namespace {

const RPCClient* g_walletNode = nullptr;

/** Conf target of Core's wallet (-txconfirmtarget). */
const int WALLET_CONFIRM_TARGET = 6;
/** When the node has no estimate: 0.0001 LTC/kB. */
const CAmount FALLBACK_FEE_PER_KB = 10000;

int64_t VirtualSize(const CTransaction& tx)
{
    int64_t weight = ::GetSerializeSize(tx, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * 3 + ::GetSerializeSize(tx, PROTOCOL_VERSION);
    return (weight + 3) / 4;
}

/** The pending transaction of createTransaction: signed, sent on commit. */
class NodePendingTx : public interfaces::PendingWalletTx
{
public:
    NodePendingTx(CTransactionRef tx, std::shared_ptr<CWallet> wallet) : m_tx(std::move(tx)), m_wallet(std::move(wallet)) {}

    const CTransaction& get() override { return *m_tx; }

    bool commit(interfaces::WalletValueMap, interfaces::WalletOrderForm, std::string& reject_reason) override
    {
        if (!m_wallet->Send(*m_tx, reject_reason)) return false;
        // As AcceptToMemoryPool in Core: known as pending at once, not only
        // after the next look at the node's mempool.
        mempool.Add(m_tx->GetHash());
        TryToAddToMarkerCache(m_tx);
        return true;
    }

private:
    CTransactionRef m_tx;
    std::shared_ptr<CWallet> m_wallet;
};

/** Throws the reason as an RPC error with OmniLite's code, so the caller learns more than the code's text. */
[[noreturn]] void ThrowWithReason(int code, const std::string& reason)
{
    PrintToLog("%s: ERROR: %s\n", __func__, reason);
    throw JSONRPCError(code, error_str(code) + ": " + reason);
}

} // namespace

// --- wallet/wallet.h ---

void SetWalletNode(const RPCClient* node)
{
    g_walletNode = node;
}

bool HasWallets()
{
    return g_walletNode != nullptr;
}

std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest& request)
{
    if (!g_walletNode) return nullptr; // as without wallets: OmniLite reports MP_ERR_WALLET_ACCESS
    std::string name;
    const std::string prefix = "/wallet/";
    if (request.URI.compare(0, prefix.size(), prefix) == 0) {
        size_t size = 0;
        char* decoded = evhttp_uridecode(request.URI.c_str() + prefix.size(), 0, &size);
        if (decoded) {
            name.assign(decoded, size);
            std::free(decoded);
        }
    }
    return std::make_shared<CWallet>(name, name.empty() ? *g_walletNode : g_walletNode->ForWallet(name));
}

UniValue CWallet::Call(const std::string& method, const UniValue& params) const
{
    try {
        return m_client.Call(method, params);
    } catch (const RPCError& e) {
        throw JSONRPCError(e.code, e.message);
    } catch (const std::exception& e) {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, std::string("litecoind: ") + e.what());
    }
}

std::vector<NodeCoin> CWallet::ListCoins(const std::string& address) const
{
    UniValue addresses(UniValue::VARR);
    addresses.push_back(address);
    UniValue params(UniValue::VARR);
    params.push_back(0);
    params.push_back(9999999);
    params.push_back(addresses);
    std::vector<NodeCoin> coins;
    const UniValue unspent = Call("listunspent", params);
    for (const UniValue& u : unspent.getValues()) {
        if (!find_value(u, "spendable").isTrue()) continue;
        const UniValue safe = find_value(u, "safe"); // unconfirmed from others: not trusted, as in Core
        if (safe.isBool() && !safe.get_bool()) continue;
        NodeCoin coin;
        coin.outpoint = COutPoint(uint256S(find_value(u, "txid").get_str()), find_value(u, "vout").get_int());
        std::vector<unsigned char> script = ParseHex(find_value(u, "scriptPubKey").get_str());
        coin.out = CTxOut(AmountFromValue(find_value(u, "amount")), CScript(script.begin(), script.end()));
        coin.address = find_value(u, "address").isStr() ? find_value(u, "address").get_str() : "";
        m_coins[coin.outpoint] = coin;
        coins.push_back(coin);
    }
    return coins;
}

namespace {

/** getaddressinfo, or validateaddress on nodes before 0.17. */
UniValue AddressInfo(const CWallet& wallet, const std::string& address)
{
    UniValue params(UniValue::VARR);
    params.push_back(address);
    try {
        return wallet.Call("getaddressinfo", params);
    } catch (const UniValue& e) {
        if (find_value(e, "code").get_int() != RPC_METHOD_NOT_FOUND) throw;
        return wallet.Call("validateaddress", params);
    }
}

} // namespace

isminetype CWallet::IsMine(const CTxDestination& dest) const
{
    if (!IsValidDestination(dest)) return ISMINE_NO;
    const std::string address = EncodeDestination(dest);
    auto cached = m_isMine.find(address);
    if (cached != m_isMine.end()) return cached->second;
    isminetype result = ISMINE_NO;
    try {
        UniValue info = AddressInfo(*this, address);
        if (find_value(info, "ismine").isTrue()) result = ISMINE_SPENDABLE;
        else if (find_value(info, "iswatchonly").isTrue()) result = ISMINE_WATCH_ONLY;
    } catch (const UniValue& e) {
        // No such wallet, none loaded, or several and none named: as Core,
        // where OmniLite then has no wallet and nothing is "mine".
        const int code = find_value(e, "code").get_int();
        if (code != RPC_WALLET_NOT_FOUND && code != RPC_WALLET_NOT_SPECIFIED && code != RPC_METHOD_NOT_FOUND) throw;
    }
    m_isMine[address] = result;
    return result;
}

std::vector<std::string> CWallet::ListAddresses(bool includeWatchOnly) const
{
    UniValue params(UniValue::VARR);
    params.push_back(0);
    params.push_back(UniValue(true)); // with addresses that never received anything
    params.push_back(UniValue(includeWatchOnly));
    std::vector<std::string> addresses;
    const UniValue received = Call("listreceivedbyaddress", params);
    for (const UniValue& r : received.getValues()) {
        if (!includeWatchOnly && find_value(r, "involvesWatchonly").isTrue()) continue;
        addresses.push_back(find_value(r, "address").get_str());
    }
    return addresses;
}

std::vector<CWallet::Tx> CWallet::ListTransactions() const
{
    UniValue params(UniValue::VARR);
    params.push_back("*");
    params.push_back(std::numeric_limits<int>::max());
    params.push_back(0);
    params.push_back(UniValue(true)); // watch-only, as OmniLite lists them
    std::vector<Tx> txs;
    std::set<uint256> seen;
    const UniValue entries = Call("listtransactions", params);
    for (const UniValue& e : entries.getValues()) {
        // One entry per output and category; the first gives the order.
        Tx tx;
        tx.txid = uint256S(find_value(e, "txid").get_str());
        if (!seen.insert(tx.txid).second) continue;
        const UniValue block = find_value(e, "blockhash");
        if (block.isStr()) tx.blockHash = uint256S(block.get_str());
        txs.push_back(tx);
    }
    return txs;
}

bool CWallet::GetPubKey(const std::string& address, CPubKey& pubKey) const
{
    UniValue info = AddressInfo(*this, address);
    const UniValue hex = find_value(info, "pubkey");
    if (!hex.isStr() || !IsHex(hex.get_str())) return false;
    pubKey = CPubKey(ParseHex(hex.get_str()));
    return pubKey.IsFullyValid();
}

CFeeRate CWallet::GetFeeRate() const
{
    if (m_feePerKb >= 0) return CFeeRate(m_feePerKb);
    CAmount perKb = 0;
    const UniValue payTxFee = find_value(Call("getwalletinfo"), "paytxfee");
    if (payTxFee.isNum()) perKb = AmountFromValue(payTxFee);
    if (perKb <= 0) {
        try {
            UniValue params(UniValue::VARR);
            params.push_back(WALLET_CONFIRM_TARGET);
            const UniValue feerate = find_value(Call("estimatesmartfee", params), "feerate");
            if (feerate.isNum()) perKb = AmountFromValue(feerate);
        } catch (const UniValue&) {
            // no estimate on this node
        }
    }
    if (perKb <= 0) perKb = FALLBACK_FEE_PER_KB;
    // Never below what the node relays.
    try {
        const UniValue relay = find_value(Call("getnetworkinfo"), "relayfee");
        if (relay.isNum()) perKb = std::max(perKb, AmountFromValue(relay));
        const UniValue poolMin = find_value(Call("getmempoolinfo"), "mempoolminfee");
        if (poolMin.isNum()) perKb = std::max(perKb, AmountFromValue(poolMin));
    } catch (const UniValue&) {
    }
    m_feePerKb = perKb;
    return CFeeRate(perKb);
}

CMutableTransaction CWallet::Sign(const CMutableTransaction& tx) const
{
    UniValue params(UniValue::VARR);
    params.push_back(EncodeHexTx(CTransaction(tx)));
    UniValue r;
    try {
        r = Call("signrawtransactionwithwallet", params);
    } catch (const UniValue& e) {
        if (find_value(e, "code").get_int() != RPC_METHOD_NOT_FOUND) throw;
        r = Call("signrawtransaction", params); // nodes before 0.17
    }
    if (!find_value(r, "complete").isTrue()) {
        std::string why = "not all inputs could be signed";
        const UniValue errors = find_value(r, "errors");
        if (errors.isArray() && errors.size() > 0 && find_value(errors[0], "error").isStr()) {
            why += ": " + find_value(errors[0], "error").get_str();
        }
        throw JSONRPCError(RPC_WALLET_ERROR, "litecoind: " + why);
    }
    CMutableTransaction signedTx;
    if (!DecodeHexTx(signedTx, find_value(r, "hex").get_str(), true)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "litecoind returned a signed transaction that cannot be read");
    }
    return signedTx;
}

bool CWallet::Send(const CTransaction& tx, std::string& rejectReason) const
{
    UniValue params(UniValue::VARR);
    params.push_back(EncodeHexTx(tx));
    try {
        m_client.Call("sendrawtransaction", params);
        return true;
    } catch (const RPCError& e) {
        rejectReason = e.message;
        return false;
    } catch (const std::exception& e) {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, std::string("litecoind: ") + e.what());
    }
}

// --- interfaces/wallet.h ---

namespace interfaces {

isminetype Wallet::isMine(const CTxDestination& dest)
{
    return m_wallet->IsMine(dest);
}

std::vector<WalletTx> Wallet::getWalletTxs()
{
    std::vector<WalletTx> result;
    int64_t pos = 0;
    for (const CWallet::Tx& tx : m_wallet->ListTransactions()) {
        result.push_back(WalletTx{std::make_shared<WalletTxHash>(WalletTxHash{tx.txid}), tx.blockHash, pos++});
    }
    return result;
}

std::unique_ptr<PendingWalletTx> Wallet::createTransaction(const std::vector<CRecipient>& recipients,
    const CCoinControl& coin_control, bool sign, int& change_pos, CAmount& fee, std::string& fail_reason,
    bool, CAmount min_fee)
{
    if (!sign) {
        fail_reason = "unsigned transactions are not supported";
        return nullptr;
    }
    std::vector<COutPoint> selected;
    coin_control.ListSelected(selected);
    if (selected.empty()) {
        fail_reason = "no inputs selected";
        return nullptr;
    }

    CMutableTransaction mtx;
    CAmount inTotal = 0;
    for (const COutPoint& outpoint : selected) {
        auto it = m_wallet->m_coins.find(outpoint);
        if (it == m_wallet->m_coins.end()) {
            fail_reason = "input " + outpoint.ToString() + " is not a known output of the wallet";
            return nullptr;
        }
        inTotal += it->second.out.nValue;
        mtx.vin.push_back(CTxIn(outpoint));
    }
    CAmount outTotal = 0;
    for (const CRecipient& r : recipients) {
        mtx.vout.push_back(CTxOut(r.nAmount, r.scriptPubKey));
        outTotal += r.nAmount;
    }
    if (!IsValidDestination(coin_control.destChange)) {
        fail_reason = "no change address";
        return nullptr;
    }
    if (inTotal < outTotal) {
        fail_reason = strprintf("Insufficient funds: the inputs hold %s LTC, the outputs need %s LTC", FormatMoney(inTotal), FormatMoney(outTotal));
        return nullptr;
    }

    // Change before the last output, which OmniLite reserves for the
    // reference address; at the end if there is only one output.
    const CScript changeScript = GetScriptForDestination(coin_control.destChange);
    change_pos = mtx.vout.size() >= 2 ? static_cast<int>(mtx.vout.size()) - 1 : static_cast<int>(mtx.vout.size());
    mtx.vout.insert(mtx.vout.begin() + change_pos, CTxOut(inTotal - outTotal, changeScript));

    // The fee follows the signed size; signing again after setting the
    // change does not change the size.
    const CFeeRate feeRate = m_wallet->GetFeeRate();
    int64_t vsize = VirtualSize(CTransaction(m_wallet->Sign(mtx)));
    fee = std::max(feeRate.GetFee(vsize), min_fee);
    CAmount change = inTotal - outTotal - fee;
    if (change < 0) {
        fail_reason = strprintf("Insufficient funds: the inputs hold %s LTC, outputs and fee need %s LTC", FormatMoney(inTotal), FormatMoney(outTotal + fee));
        return nullptr;
    }
    if (change < OmniGetDustThreshold(changeScript)) {
        // Not worth an output: it goes to the fee, as in Core.
        mtx.vout.erase(mtx.vout.begin() + change_pos);
        change_pos = -1;
        fee = inTotal - outTotal;
    } else {
        mtx.vout[change_pos].nValue = change;
    }
    return std::make_unique<NodePendingTx>(MakeTransactionRef(m_wallet->Sign(mtx)), m_wallet);
}

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet)
{
    return wallet ? std::make_unique<Wallet>(wallet) : nullptr;
}

} // namespace interfaces

// --- omnicore/walletutils.h: coin selection ---

namespace mastercore {

int64_t SelectCoins(interfaces::Wallet& iWallet, const std::string& fromAddress, CCoinControl& coinControl, int64_t additional)
{
    // total output funds collected
    int64_t nTotal = 0;
    int nHeight = chainActive.Height();

    // select coins to cover up to 20 kB max. transaction size
    CAmount nMax = 20 * iWallet.wallet().GetFeeRate().GetFeePerK();

    // if referenceamount is set it is needed to be accounted for here too
    if (0 < additional) nMax += additional;

    for (const NodeCoin& coin : iWallet.wallet().ListCoins(fromAddress)) {
        CTxDestination dest;
        if (!CheckInput(coin.out, nHeight, dest)) continue;
        // only use funds from the sender's address
        if (EncodeDestination(dest) != fromAddress) continue;
        if (coin.out.nValue < GetEconomicThreshold(iWallet, coin.out)) continue;

        coinControl.Select(coin.outpoint);
        nTotal += coin.out.nValue;
        if (nMax <= nTotal) break;
    }
    return nTotal;
}

int64_t SelectAllCoins(interfaces::Wallet& iWallet, const std::string& fromAddress, CCoinControl& coinControl)
{
    int64_t nTotal = 0;
    int nHeight = chainActive.Height();

    for (const NodeCoin& coin : iWallet.wallet().ListCoins(fromAddress)) {
        CTxDestination dest;
        if (!CheckInput(coin.out, nHeight, dest)) continue;
        if (EncodeDestination(dest) != fromAddress) continue;

        coinControl.Select(coin.outpoint);
        nTotal += coin.out.nValue;
    }
    return nTotal;
}

} // namespace mastercore

// --- omnicore/wallettxbuilder.h ---

/** Creates and sends a transaction; as OmniLite's, with the node's wallet doing the wallet's part. */
int WalletTxBuilder(
        const std::string& senderAddress,
        const std::string& receiverAddress,
        const std::string& redemptionAddress,
        int64_t referenceAmount,
        const std::vector<unsigned char>& payload,
        uint256& retTxid,
        std::string& retRawTx,
        bool commit,
        interfaces::Wallet* iWallet,
        CAmount minFee)
{
    if (!iWallet) return MP_ERR_WALLET_ACCESS;

    // Determine the class to send the transaction via - default is Class C
    int omniTxClass = OMNI_CLASS_C;
    if (!mastercore::UseEncodingClassC(payload.size() + 1 /* OP_RETURN */ + 2 /* pushdata opcodes */)) omniTxClass = OMNI_CLASS_B;

    // Prepare the transaction - first setup some vars
    CCoinControl coinControl;
    std::vector<std::pair<CScript, int64_t> > vecSend;

    // Next, we set the change address to the sender
    coinControl.destChange = DecodeDestination(senderAddress);

    // Select the inputs
    if (0 > mastercore::SelectCoins(*iWallet, senderAddress, coinControl, referenceAmount)) { return MP_INPUTS_INVALID; }

    // Encode the data outputs
    switch(omniTxClass) {
        case OMNI_CLASS_B: {
            const std::string& sAddress = redemptionAddress.empty() ? senderAddress : redemptionAddress;
            CPubKey redeemingPubKey;
            // A wallet address, or a hex-encoded public key (as mastercore::AddressToPubKey).
            if (!iWallet->wallet().GetPubKey(sAddress, redeemingPubKey) && IsHex(sAddress)) {
                redeemingPubKey = CPubKey(ParseHex(sAddress));
            }
            if (!redeemingPubKey.IsFullyValid()) {
                PrintToLog("%s: ERROR: no public key for redemption address %s\n", __func__, sAddress);
                return MP_REDEMP_BAD_VALIDATION;
            }
            if (!OmniCore_Encode_ClassB(senderAddress,redeemingPubKey,payload,vecSend)) { return MP_ENCODING_ERROR; }
        break; }
        case OMNI_CLASS_C:
            if(!OmniCore_Encode_ClassC(payload,vecSend)) { return MP_ENCODING_ERROR; }
        break;
    }

    // Then add a paytopubkeyhash output for the recipient (if needed) - note we do this last as we want this to be the highest vout
    if (!receiverAddress.empty()) {
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(receiverAddress));
        vecSend.push_back(std::make_pair(scriptPubKey, 0 < referenceAmount ? referenceAmount : OmniGetDustThreshold(scriptPubKey)));
    }

    if (!coinControl.HasSelected()) {
        ThrowWithReason(MP_ERR_INPUTSELECT_FAIL, "no spendable outputs of " + senderAddress + " in the node's wallet");
    }

    std::vector<CRecipient> vecRecipients;
    for (const auto& vec : vecSend) vecRecipients.push_back({vec.first, vec.second, false});

    // Ask the wallet to create the transaction
    CAmount nFeeRet = 0;
    int nChangePosInOut = -1;
    std::string strFailReason;
    auto wtxNew = iWallet->createTransaction(vecRecipients, coinControl, true /* sign */, nChangePosInOut, nFeeRet, strFailReason, false, minFee);
    if (!wtxNew) ThrowWithReason(MP_ERR_CREATE_TX, strFailReason);

    // If this request is only to create, but not commit the transaction then display it and exit
    if (!commit) {
        retRawTx = EncodeHexTx(wtxNew->get());
        return 0;
    }
    PrintToLog("%s: %s; nFeeRet = %d\n", __func__, wtxNew->get().ToString(), nFeeRet);
    std::string rejectReason;
    if (!wtxNew->commit({}, {}, rejectReason)) ThrowWithReason(MP_ERR_COMMIT_TX, rejectReason);
    retTxid = wtxNew->get().GetHash();
    return 0;
}

/**
 * Creates and sends a raw transaction by selecting all coins from the sender
 * and enough coins from a fee source. Change is sent to the fee source!
 * As OmniLite's: the sender's inputs come first, the change output first.
 */
int CreateFundedTransaction(
        const std::string& senderAddress,
        const std::string& receiverAddress,
        const std::string& feeAddress,
        const std::vector<unsigned char>& payload,
        uint256& retTxid,
        interfaces::Wallet* iWallet)
{
    if (!iWallet) return MP_ERR_WALLET_ACCESS;
    if (!mastercore::UseEncodingClassC(payload.size())) return MP_ENCODING_ERROR;

    // add payload output
    std::vector<std::pair<CScript, int64_t> > vecSend;
    if (!OmniCore_Encode_ClassC(payload, vecSend)) return MP_ENCODING_ERROR;

    // add reference output, if there is one
    if (!receiverAddress.empty() && receiverAddress != feeAddress) {
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(receiverAddress));
        vecSend.push_back(std::make_pair(scriptPubKey, OmniGetDustThreshold(scriptPubKey)));
    }

    CCoinControl coinControl;
    if (!mastercore::SelectAllCoins(*iWallet, senderAddress, coinControl)) {
        PrintToLog("%s: ERROR: sender %s has no coins\n", __func__, senderAddress);
        return MP_INPUTS_INVALID;
    }

    CWallet& wallet = iWallet->wallet();
    CMutableTransaction mtx;
    CAmount inTotal = 0;
    std::vector<COutPoint> senderInputs;
    coinControl.ListSelected(senderInputs);
    for (const COutPoint& outpoint : senderInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
        inTotal += wallet.m_coins.at(outpoint).out.nValue;
    }

    // Coins of the fee source, after the sender's.
    const int nHeight = chainActive.Height();
    std::vector<NodeCoin> feeCoins;
    for (const NodeCoin& coin : wallet.ListCoins(feeAddress)) {
        CTxDestination dest;
        if (coinControl.IsSelected(coin.outpoint)) continue;
        if (!mastercore::CheckInput(coin.out, nHeight, dest) || EncodeDestination(dest) != feeAddress) continue;
        feeCoins.push_back(coin);
    }

    const CScript changeScript = GetScriptForDestination(DecodeDestination(feeAddress));
    mtx.vout.push_back(CTxOut(0, changeScript)); // add change first
    CAmount outTotal = 0;
    for (const auto& vec : vecSend) {
        mtx.vout.push_back(CTxOut(vec.second, vec.first));
        outTotal += vec.second;
    }

    const CFeeRate feeRate = wallet.GetFeeRate();
    size_t nextFeeCoin = 0;
    while (true) {
        mtx.vout[0].nValue = std::max<CAmount>(0, inTotal - outTotal);
        CAmount fee = feeRate.GetFee(VirtualSize(CTransaction(wallet.Sign(mtx))));
        CAmount change = inTotal - outTotal - fee;
        if (change >= 0) {
            if (change < OmniGetDustThreshold(changeScript)) {
                if (receiverAddress == feeAddress) ThrowWithReason(MP_ERR_CREATE_TX, "send to self without change");
                mtx.vout.erase(mtx.vout.begin());
            } else {
                mtx.vout[0].nValue = change;
            }
            break;
        }
        if (nextFeeCoin == feeCoins.size()) {
            ThrowWithReason(MP_ERR_CREATE_TX, strprintf("Insufficient funds: %s holds too little to pay the fee of %s LTC", feeAddress, FormatMoney(fee)));
        }
        mtx.vin.push_back(CTxIn(feeCoins[nextFeeCoin].outpoint));
        inTotal += feeCoins[nextFeeCoin].out.nValue;
        ++nextFeeCoin;
    }

    CTransactionRef tx = MakeTransactionRef(wallet.Sign(mtx));
    std::string rejectReason;
    if (!wallet.Send(*tx, rejectReason)) ThrowWithReason(MP_ERR_COMMIT_TX, rejectReason);
    mempool.Add(tx->GetHash());
    TryToAddToMarkerCache(tx);
    retTxid = tx->GetHash();
    return 0;
}
