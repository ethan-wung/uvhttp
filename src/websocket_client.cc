/**
 * @file        websocket_client.cc
 * @brief       WebSocket 明文及 TLS 升级客户端的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/websocket_client.h"

#include <array>
#include <random>
#include <utility>



#include "http1_parser.h"
#include "http_internal.h"
#include "uvhttp/tls_stream.h"

namespace uvhttp
{
namespace
{
const char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const unsigned char *data, std::size_t size)
{
    std::string result;
    result.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3)
    {
        const unsigned value = static_cast<unsigned>(data[i]) << 16 |
                               (i + 1 < size ? static_cast<unsigned>(data[i + 1]) << 8 : 0) |
                               (i + 2 < size ? data[i + 2] : 0);
        result.push_back(base64Alphabet[(value >> 18) & 63]);
        result.push_back(base64Alphabet[(value >> 12) & 63]);
        result.push_back(i + 1 < size ? base64Alphabet[(value >> 6) & 63] : '=');
        result.push_back(i + 2 < size ? base64Alphabet[value & 63] : '=');
    }
    return result;
}

std::string randomKey()
{
    std::array<unsigned char, 16> bytes{};
    std::random_device device;
    for (auto &byte : bytes)
        byte = static_cast<unsigned char>(device());
    return base64(bytes.data(), bytes.size());
}

bool hasToken(const HttpHeaders &headers, std::string_view name)
{
    return detail::hasConnectionToken(headers, name);
}

struct Pending : std::enable_shared_from_this<Pending>
{
    WebSocketClient::Callbacks callbacks;
    std::string host;
    std::string target;
    std::string key;
    std::size_t maxPayload{0};
    std::unique_ptr<detail::Http1Parser> parser;
    AsyncByteStream::Ptr stream;
    TlsStream::Ptr tls;
    WebSocketConnection::Ptr connection;
    bool connected{false};
    bool failed{false};
    HttpResponseHead responseHead;

    void fail(Error error)
    {
        if (failed || connected) return;
        failed = true;
        if (callbacks.onError) callbacks.onError(error);
        if (stream) stream->closeStream();
    }

    void sendHandshake()
    {
        std::string request = "GET " + (target.empty() ? "/" : target) + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
        request += "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (stream->writeBytes(std::move(request)) == AsyncByteStream::WriteStatus::Closed)
            fail(Error{UV_EPIPE, "failed to send WebSocket handshake"});
    }

    void receive(std::string_view bytes)
    {
        if (failed) return;
        if (connected)
        { connection->feed(bytes); return; }
        parser->feed(bytes);
    }

    void completeHandshake()
    {
        if (failed || connected) return;
        auto buffered = parser->takeBuffered();
        if (responseHead.status != 101 ||
            detail::lowerAscii(std::string(responseHead.headers.get("Upgrade"))) != "websocket" ||
            !hasToken(responseHead.headers, "Upgrade") ||
            std::string(responseHead.headers.get("Sec-WebSocket-Accept")) != websocketAcceptKey(key))
        {
            fail(Error{UV_EPROTO, "invalid WebSocket handshake response"});
            return;
        }
        connected = true;
        connection = WebSocketConnection::create(stream, false, {}, maxPayload);
        if (callbacks.onConnected) callbacks.onConnected(connection);
        if (!buffered.empty()) connection->feed(buffered);
    }
};

bool start(WebSocketClient::Options options, TcpClient &client, std::string host,
           std::uint16_t port, std::string target, WebSocketClient::Callbacks callbacks,
           SSL_CTX *context, bool verifyPeer)
{
    auto pending = std::make_shared<Pending>();
    pending->callbacks = std::move(callbacks);
    pending->host = host;
    pending->target = std::move(target);
    pending->key = randomKey();
    pending->maxPayload = options.maxPayloadBytes;
    detail::Http1Parser::Callbacks parserCallbacks;
    parserCallbacks.onHead = [pending](const detail::Http1MessageHead &message) {
        if (!message.request && message.responseHead.status >= 100 && message.responseHead.status < 200 &&
            message.responseHead.status != 101)
            return;
        pending->responseHead = message.responseHead;
        if (message.request || message.responseHead.status != 101)
            pending->fail(Error{UV_EPROTO, "WebSocket upgrade was rejected"});
    };
    parserCallbacks.onComplete = [pending] { pending->completeHandshake(); };
    parserCallbacks.onError = [pending](const Error &error) { pending->fail(error); };
    pending->parser = std::make_unique<detail::Http1Parser>(detail::Http1Parser::Kind::Response,
                                                             options.maxHeaderBytes,
                                                             std::move(parserCallbacks));

    if (context)
    {
        TlsStream::Callbacks tlsCallbacks;
        tlsCallbacks.onHandshake = [pending](const TlsStream::Ptr &) { pending->sendHandshake(); };
        tlsCallbacks.onData = [pending](const TlsStream::Ptr &, std::string_view bytes) { pending->receive(bytes); };
        tlsCallbacks.onError = [pending](const Error &error) { pending->fail(error); };
        tlsCallbacks.onClosed = [pending] { pending->fail(Error{UV_EOF, "WebSocket TLS stream closed"}); };
        if (!TlsStream::connect(client.eventLoop(), client, std::move(host), port, context,
                                std::move(tlsCallbacks), {"http/1.1"}, verifyPeer))
            return false;
        return true;
    }

    TcpConnection::Callbacks tcpCallbacks;
    tcpCallbacks.onData = [pending](const TcpConnection::Ptr &, std::string_view bytes) { pending->receive(bytes); };
    tcpCallbacks.onError = [pending](const TcpConnection::Ptr &, const Error &error) { pending->fail(error); };
    tcpCallbacks.onClosed = [pending](const TcpConnection::Ptr &) { pending->fail(Error{UV_EOF, "WebSocket stream closed"}); };
    return client.connect(std::move(host), port, std::move(tcpCallbacks),
        [pending](const TcpConnection::Ptr &connection, const Error &error) {
            if (error) { pending->fail(error); return; }
            pending->stream = connection;
            pending->sendHandshake();
        });
}
}  // namespace

WebSocketClient::WebSocketClient(EventLoop &loop) : WebSocketClient(loop, Options{}) {}
WebSocketClient::WebSocketClient(EventLoop &loop, Options options)
    : loop_(loop), options_(std::move(options)), tcpClient_(loop_, options_.tcp) {}
WebSocketClient::~WebSocketClient() = default;

bool WebSocketClient::connect(std::string host, std::uint16_t port, std::string target, Callbacks callbacks)
{
    return start(options_, tcpClient_, std::move(host), port, std::move(target), std::move(callbacks), nullptr, false);
}

bool WebSocketClient::connectTls(std::string host, std::uint16_t port, SSL_CTX *context,
                                  std::string target, Callbacks callbacks, bool verifyPeer)
{
    if (!context) return false;
    return start(options_, tcpClient_, std::move(host), port, std::move(target), std::move(callbacks), context, verifyPeer);
}

}  // namespace uvhttp
