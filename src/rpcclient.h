// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_RPCCLIENT_H
#define OMNILITE_STANDALONE_RPCCLIENT_H

// A minimal JSON-RPC client for litecoind over libevent, like
// bitcoin-cli. Only standard Litecoin Core calls are used, so any node
// version that offers them works.

#include <rpc/protocol.h> // Litecoin Core's error codes
#include <univalue.h>

#include <stdexcept>
#include <string>

/** An error returned by the node itself (the "error" member of the reply). */
class RPCError : public std::runtime_error
{
public:
    RPCError(int code, const std::string& message);
    int code;
    std::string message;
};

/** The node could not be reached or answered with something that is not JSON-RPC. */
class NodeUnreachable : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/** The node rejected the credentials. */
class NodeUnauthorized : public std::runtime_error
{
public:
    NodeUnauthorized();
};

class RPCClient
{
public:
    RPCClient(std::string host, int port, std::string user, std::string password, int timeoutSeconds = 120);

    /** Calls method with params (an array) and returns the result. */
    UniValue Call(const std::string& method, const UniValue& params = UniValue(UniValue::VARR)) const;

    /** A client for one wallet of a node with several (/wallet/<name>). */
    RPCClient ForWallet(const std::string& name) const;

    std::string URL() const;

private:
    std::string m_host;
    int m_port;
    std::string m_user;
    std::string m_password;
    int m_timeout;
    std::string m_path = "/";
};

/** Whether the node runs with -txindex, and whether that index has caught up. */
struct TxIndexStatus {
    bool enabled = false;
    bool synced = false;
};

/**
 * Checks for -txindex: getindexinfo on nodes from 0.21, else a lookup of
 * the coinbase of block 1, which only the transaction index can find.
 */
TxIndexStatus GetTxIndexStatus(const RPCClient& node);

#endif // OMNILITE_STANDALONE_RPCCLIENT_H
