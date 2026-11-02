// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpcclient.h>

#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>

namespace {

/**
 * A fake litecoind on a local port. It answers like Litecoin Core: the
 * method name as result, an error object for "fail", HTTP 401 for a wrong
 * password, and the request path for "path".
 */
class FakeNode
{
public:
    FakeNode()
    {
        evthread_use_pthreads();
        m_base = event_base_new();
        m_http = evhttp_new(m_base);
        evhttp_set_gencb(m_http, Handle, this);
        m_socket = evhttp_bind_socket_with_handle(m_http, "127.0.0.1", 0);
        BOOST_REQUIRE(m_socket);
        sockaddr_storage addr{};
        ev_socklen_t len = sizeof(addr);
        getsockname(evhttp_bound_socket_get_fd(m_socket), reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
        // Stopping goes through an event, so it also works before the loop runs.
        m_stop = event_new(m_base, -1, 0, [](evutil_socket_t, short, void* base) { event_base_loopbreak(static_cast<event_base*>(base)); }, m_base);
        m_thread = std::thread([this] { event_base_loop(m_base, EVLOOP_NO_EXIT_ON_EMPTY); });
    }
    ~FakeNode()
    {
        event_active(m_stop, 0, 0);
        m_thread.join();
        event_free(m_stop);
        evhttp_free(m_http);
        event_base_free(m_base);
    }
    int port = 0;

private:
    static void Handle(evhttp_request* req, void*)
    {
        evkeyvalq* in = evhttp_request_get_input_headers(req);
        const char* auth = evhttp_find_header(in, "Authorization");
        evbuffer* out = evbuffer_new();
        if (!auth || std::string(auth) != "Basic " + EncodeBase64("user:pass")) {
            evhttp_send_reply(req, 401, "Unauthorized", out);
            evbuffer_free(out);
            return;
        }
        evbuffer* body = evhttp_request_get_input_buffer(req);
        size_t n = evbuffer_get_length(body);
        UniValue request;
        request.read(std::string(reinterpret_cast<const char*>(evbuffer_pullup(body, n)), n));
        std::string method = find_value(request, "method").get_str();
        UniValue reply(UniValue::VOBJ);
        int status = 200;
        if (method == "fail") {
            UniValue error(UniValue::VOBJ);
            error.pushKV("code", -5);
            error.pushKV("message", "No such mempool or blockchain transaction");
            reply.pushKV("result", NullUniValue);
            reply.pushKV("error", error);
            status = 500;
        } else if (method == "path") {
            reply.pushKV("result", evhttp_request_get_uri(req));
            reply.pushKV("error", NullUniValue);
        } else {
            reply.pushKV("result", method + "/" + std::to_string(find_value(request, "params").size()));
            reply.pushKV("error", NullUniValue);
        }
        reply.pushKV("id", find_value(request, "id"));
        std::string text = reply.write();
        evbuffer_add(out, text.data(), text.size());
        evhttp_send_reply(req, status, "", out);
        evbuffer_free(out);
    }

    event_base* m_base;
    event* m_stop;
    evhttp* m_http;
    evhttp_bound_socket* m_socket;
    std::thread m_thread;
};

} // namespace

BOOST_AUTO_TEST_SUITE(rpcclient_tests)

BOOST_AUTO_TEST_CASE(call)
{
    FakeNode fake;
    RPCClient node("127.0.0.1", fake.port, "user", "pass", 10);
    UniValue params(UniValue::VARR);
    params.push_back(1);
    params.push_back("x");
    BOOST_CHECK_EQUAL(node.Call("getblockhash", params).get_str(), "getblockhash/2");
    BOOST_CHECK_EQUAL(node.Call("getblockcount").get_str(), "getblockcount/0");
    BOOST_CHECK_EQUAL(node.Call("path").get_str(), "/");
    BOOST_CHECK_EQUAL(node.ForWallet("my wallet").Call("path").get_str(), "/wallet/my%20wallet");
}

BOOST_AUTO_TEST_CASE(errors)
{
    FakeNode fake;
    RPCClient node("127.0.0.1", fake.port, "user", "pass", 10);
    try {
        node.Call("fail");
        BOOST_ERROR("no exception");
    } catch (const RPCError& e) {
        BOOST_CHECK_EQUAL(e.code, RPC_INVALID_ADDRESS_OR_KEY);
        BOOST_CHECK_EQUAL(e.message, "No such mempool or blockchain transaction");
    }
    RPCClient wrong("127.0.0.1", fake.port, "user", "nope", 10);
    BOOST_CHECK_THROW(wrong.Call("getblockcount"), NodeUnauthorized);
}

BOOST_AUTO_TEST_CASE(unreachable)
{
    int port;
    {
        FakeNode fake;
        port = fake.port;
    }
    RPCClient node("127.0.0.1", port, "user", "pass", 5);
    BOOST_CHECK_THROW(node.Call("getblockcount"), NodeUnreachable);
}

BOOST_AUTO_TEST_SUITE_END()
