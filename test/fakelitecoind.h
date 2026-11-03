// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OMNILITE_STANDALONE_TEST_FAKELITECOIND_H
#define OMNILITE_STANDALONE_TEST_FAKELITECOIND_H

#include <univalue.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <functional>
#include <string>
#include <thread>

/**
 * A fake litecoind on a local port. It answers like Litecoin Core: the
 * method name as result, an error object for "fail", HTTP 401 for a wrong
 * password, and the request path for "path".
 */
class FakeLitecoind
{
public:
    FakeLitecoind()
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
    ~FakeLitecoind()
    {
        event_active(m_stop, 0, 0);
        m_thread.join();
        event_free(m_stop);
        evhttp_free(m_http);
        event_base_free(m_base);
    }
    int port = 0;

    /**
     * Answers before the built-in replies, if set and it returns true: the
     * method, its params and the request path in, the result or an error
     * object ({code, message}) out. Runs on the server's thread.
     */
    std::function<bool(const std::string& method, const UniValue& params, const std::string& uri, UniValue& result, UniValue& error)> handler;

private:
    static void Handle(evhttp_request* req, void* self)
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
        UniValue result, error;
        FakeLitecoind* fake = static_cast<FakeLitecoind*>(self);
        if (fake->handler && fake->handler(method, find_value(request, "params"), evhttp_request_get_uri(req), result, error)) {
            reply.pushKV("result", error.isNull() ? result : NullUniValue);
            reply.pushKV("error", error);
            if (!error.isNull()) status = 500;
        } else if (method == "fail") {
            UniValue error(UniValue::VOBJ);
            error.pushKV("code", -5);
            error.pushKV("message", "No such mempool or blockchain transaction");
            reply.pushKV("result", NullUniValue);
            reply.pushKV("error", error);
            status = 500;
        } else if (method == "getblockhash" && find_value(request, "params")[0].get_int64() > 5) {
            UniValue error(UniValue::VOBJ);
            error.pushKV("code", -8);
            error.pushKV("message", "Block height out of range");
            reply.pushKV("result", NullUniValue);
            reply.pushKV("error", error);
            status = 500;
        } else if (method == "getblockheader") {
            UniValue header(UniValue::VOBJ);
            header.pushKV("hash", find_value(request, "params")[0]);
            header.pushKV("height", 5);
            header.pushKV("previousblockhash", "parent");
            header.pushKV("time", 1700000000);
            reply.pushKV("result", header);
            reply.pushKV("error", NullUniValue);
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

#endif // OMNILITE_STANDALONE_TEST_FAKELITECOIND_H
