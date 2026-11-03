// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpcclient.h>

#include <tinyformat.h>
#include <util/strencodings.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <atomic>
#include <cctype>
#include <memory>

RPCError::RPCError(int code, const std::string& message)
    : std::runtime_error("rpc error " + std::to_string(code) + ": " + message), code(code), message(message)
{
}

NodeUnauthorized::NodeUnauthorized()
    : std::runtime_error("the node rejected the login, check rpcuser/rpcpassword or the cookie file")
{
}

namespace {

std::atomic<int> nextID{1};

struct HTTPReply {
    int status = 0;
    int error = -1;
    std::string body;
};

void HTTPRequestDone(struct evhttp_request* req, void* ctx)
{
    HTTPReply* reply = static_cast<HTTPReply*>(ctx);
    if (req == nullptr) {
        // Connection failed; the error callback has the reason.
        return;
    }
    reply->status = evhttp_request_get_response_code(req);
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
    if (buf) {
        size_t size = evbuffer_get_length(buf);
        const char* data = reinterpret_cast<const char*>(evbuffer_pullup(buf, size));
        if (data) reply->body.assign(data, size);
        evbuffer_drain(buf, size);
    }
}

void HTTPErrorCallback(enum evhttp_request_error err, void* ctx)
{
    static_cast<HTTPReply*>(ctx)->error = err;
}

const char* HTTPErrorString(int code)
{
    switch (code) {
    case EVREQ_HTTP_TIMEOUT: return "timeout reached";
    case EVREQ_HTTP_EOF: return "EOF reached";
    case EVREQ_HTTP_INVALID_HEADER: return "error while reading header, or invalid header";
    case EVREQ_HTTP_BUFFER_ERROR: return "error encountered while reading or writing";
    case EVREQ_HTTP_REQUEST_CANCEL: return "request was canceled";
    case EVREQ_HTTP_DATA_TOO_LONG: return "response body is larger than allowed";
    default: return "unknown";
    }
}

} // namespace

RPCClient::RPCClient(std::string host, int port, std::string user, std::string password, int timeoutSeconds)
    : m_host(std::move(host)), m_port(port), m_user(std::move(user)), m_password(std::move(password)), m_timeout(timeoutSeconds)
{
}

RPCClient RPCClient::ForWallet(const std::string& name) const
{
    RPCClient c = *this;
    if (!name.empty()) {
        c.m_path = "/wallet/";
        for (unsigned char ch : name) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                c.m_path += static_cast<char>(ch);
            } else {
                c.m_path += strprintf("%%%02X", ch);
            }
        }
    }
    return c;
}

std::string RPCClient::URL() const
{
    return "http://" + m_host + ":" + std::to_string(m_port);
}

UniValue RPCClient::Call(const std::string& method, const UniValue& params) const
{
    UniValue request(UniValue::VOBJ);
    request.pushKV("jsonrpc", "1.0");
    request.pushKV("id", nextID++);
    request.pushKV("method", method);
    request.pushKV("params", params);
    std::string body = request.write();

    std::unique_ptr<event_base, decltype(&event_base_free)> base(event_base_new(), &event_base_free);
    if (!base) throw std::runtime_error("cannot create event base");
    std::unique_ptr<evhttp_connection, decltype(&evhttp_connection_free)> conn(
        evhttp_connection_base_new(base.get(), nullptr, m_host.c_str(), m_port), &evhttp_connection_free);
    if (!conn) throw std::runtime_error("cannot create connection");
    evhttp_connection_set_timeout(conn.get(), m_timeout);

    HTTPReply reply;
    // libevent frees the request once it is done.
    struct evhttp_request* req = evhttp_request_new(HTTPRequestDone, &reply);
    if (!req) throw std::runtime_error("cannot create request");
    evhttp_request_set_error_cb(req, HTTPErrorCallback);

    struct evkeyvalq* headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Host", m_host.c_str());
    evhttp_add_header(headers, "Connection", "close");
    evhttp_add_header(headers, "Content-Type", "application/json");
    std::string auth = "Basic " + EncodeBase64(m_user + ":" + m_password);
    evhttp_add_header(headers, "Authorization", auth.c_str());
    evbuffer_add(evhttp_request_get_output_buffer(req), body.data(), body.size());

    if (evhttp_make_request(conn.get(), req, EVHTTP_REQ_POST, m_path.c_str()) != 0) {
        throw NodeUnreachable(method + ": cannot send request to " + URL());
    }
    event_base_dispatch(base.get());

    if (reply.status == 0) {
        std::string reason = reply.error == -1 ? "connection refused or no answer" : HTTPErrorString(reply.error);
        throw NodeUnreachable(method + ": no answer from " + URL() + " (" + reason + ")");
    }
    if (reply.status == 401) throw NodeUnauthorized();

    // litecoind answers RPC errors with HTTP 500 and a JSON body, so the
    // status code alone says nothing; try the body first.
    UniValue response;
    if (!response.read(reply.body) || !response.isObject()) {
        std::string snippet = reply.body.substr(0, 200);
        throw NodeUnreachable(method + ": HTTP " + std::to_string(reply.status) + " from " + URL() + ": " + snippet);
    }
    const UniValue error = find_value(response, "error");
    if (!error.isNull()) {
        const UniValue code = find_value(error, "code");
        const UniValue message = find_value(error, "message");
        throw RPCError(code.isNum() ? code.get_int() : 0, message.isStr() ? message.get_str() : error.write());
    }
    return find_value(response, "result");
}

TxIndexStatus GetTxIndexStatus(const RPCClient& node)
{
    TxIndexStatus status;
    try {
        UniValue info = node.Call("getindexinfo");
        const UniValue txindex = find_value(info, "txindex");
        if (txindex.isObject()) {
            status.enabled = true;
            status.synced = find_value(txindex, "synced").isTrue();
        }
        return status;
    } catch (const RPCError& e) {
        if (e.code != RPC_METHOD_NOT_FOUND) throw;
    }

    // Nodes before 0.21 have no getindexinfo.
    UniValue params(UniValue::VARR);
    params.push_back(1);
    std::string hash = node.Call("getblockhash", params).get_str();
    UniValue blockParams(UniValue::VARR);
    blockParams.push_back(hash);
    blockParams.push_back(1);
    UniValue block = node.Call("getblock", blockParams);
    const UniValue txs = find_value(block, "tx");
    if (!txs.isArray() || txs.empty()) {
        throw std::runtime_error("block 1 has no transactions");
    }
    UniValue txParams(UniValue::VARR);
    txParams.push_back(txs[0].get_str());
    txParams.push_back(UniValue(false));
    try {
        node.Call("getrawtransaction", txParams);
    } catch (const RPCError& e) {
        if (e.code == RPC_INVALID_ADDRESS_OR_KEY) return status;
        throw;
    }
    // Old nodes build the index before serving RPC.
    status.enabled = status.synced = true;
    return status;
}
