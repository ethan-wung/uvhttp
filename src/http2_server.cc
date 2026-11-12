/**
 * @file        http2_server.cc
 * @brief       HTTP/2 明文及 TLS/ALPN 服务端接口的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http2_server.h"

#include <unordered_map>
#include <utility>

#include "uvhttp/http2_session.h"
#include "uvhttp/tls_stream.h"

namespace uvhttp
{
struct Http2Server::Impl : std::enable_shared_from_this<Impl>
{
    Impl(EventLoop &eventLoop, std::string bindAddress, std::uint16_t bindPort,
         Options serverOptions, Callbacks serverCallbacks)
        : loop(eventLoop), address(std::move(bindAddress)), requestedPort(bindPort),
          options(std::move(serverOptions)), callbacks(std::move(serverCallbacks)) {}

    Http2Session::Ptr createSession(TcpConnection *key, AsyncByteStream::Ptr transport)
    {
        auto weak = weak_from_this();
        Http2Session::Callbacks h2;
        h2.onRequest = [weak](const Http2Session::Ptr &session, std::int32_t id, HttpRequest request) {
            if (auto self = weak.lock())
            {
                if (self->callbacks.onRequest) self->callbacks.onRequest(session, id, std::move(request));
                else { HttpResponse response; response.status = 404; session->respond(id, std::move(response)); }
            }
        };
        h2.onRequestHead = [weak](const Http2Session::Ptr &session, std::int32_t id, HttpRequestHead head) {
            if (auto self = weak.lock(); self && self->callbacks.onRequestHead)
                self->callbacks.onRequestHead(session, id, std::move(head));
        };
        if (callbacks.onRequestHead || callbacks.onRequestBodyWithSession || callbacks.onRequestBody || callbacks.onRequestEnd) h2.onRequestBody = [weak](std::int32_t id, std::string_view body) {
            if (auto self = weak.lock(); self && self->callbacks.onRequestBody)
                self->callbacks.onRequestBody(id, body);
        };
        if (callbacks.onRequestHead || callbacks.onRequestBodyWithSession || callbacks.onRequestBody || callbacks.onRequestEnd) h2.onRequestEnd = [weak](const Http2Session::Ptr &session, std::int32_t id) {
            if (auto self = weak.lock(); self && self->callbacks.onRequestEnd)
                self->callbacks.onRequestEnd(session, id);
        };
        if(callbacks.onRequestBodyWithSession) h2.onRequestBodyWithSession=[weak](const Http2Session::Ptr &session,std::int32_t id,std::string_view body) {
            if(auto self=weak.lock(); self && self->callbacks.onRequestBodyWithSession) self->callbacks.onRequestBodyWithSession(session,id,body);
        };
        h2.onError = [weak](const Error &error) {
            if (auto self = weak.lock(); self && self->callbacks.onError)
                self->callbacks.onError(error);
        };
        auto session = Http2Session::createServer(std::move(transport), std::move(h2),
                                                  options.maxConcurrentStreams, options.maxRequestBodyBytes, options.maxHeaderBytes);
        if (session) sessions[key] = session;
        return session;
    }

    void initialize()
    {
        auto weak = weak_from_this();
        TcpServer::Callbacks tcpCallbacks;
        tcpCallbacks.onListening = [weak](std::uint16_t value) {
            if (auto self = weak.lock())
            {
                self->boundPort = value;
                if (self->callbacks.onListening) self->callbacks.onListening(value);
            }
        };
        tcpCallbacks.onConnection = [weak](const TcpConnection::Ptr &tcp) {
            auto self = weak.lock();
            if (!self) return;
            auto *key = tcp.get();
            if (!self->options.tlsEnabled)
            {
                self->createSession(key, tcp);
                return;
            }
            if (!self->options.tlsContext)
            {
                tcp->close();
                return;
            }
            TlsStream::Callbacks tlsCallbacks;
            tlsCallbacks.onHandshake = [weak, key](const TlsStream::Ptr &stream) {
                if (auto current = weak.lock())
                {
                    if (stream->negotiatedAlpn() != "h2") { stream->close(); return; }
                    current->createSession(key, stream);
                }
            };
            tlsCallbacks.onData = [weak, key](const TlsStream::Ptr &, std::string_view bytes) {
                if (auto current = weak.lock())
                {
                    auto found = current->sessions.find(key);
                    if (found != current->sessions.end()) found->second->receive(bytes);
                }
            };
            tlsCallbacks.onError = [weak](const Error &error) {
                if (error.code == UV_EOF) return;
                if (auto current = weak.lock(); current && current->callbacks.onError)
                    current->callbacks.onError(error);
            };
            auto tls = TlsStream::accept(tcp, self->options.tlsContext,
                                         std::move(tlsCallbacks), {"h2"});
            if (tls) self->tls.emplace(key, std::move(tls));
        };
        tcpCallbacks.onData = [weak](const TcpConnection::Ptr &tcp, std::string_view bytes) {
            if (auto self = weak.lock())
            {
                auto tls = self->tls.find(tcp.get());
                if (tls != self->tls.end()) { tls->second->feedEncrypted(bytes); return; }
                auto session = self->sessions.find(tcp.get());
                if (session != self->sessions.end()) session->second->receive(bytes);
            }
        };
        tcpCallbacks.onConnectionClosed = [weak](const TcpConnection::Ptr &tcp) {
            if (auto self = weak.lock())
            {
                self->sessions.erase(tcp.get());
                self->tls.erase(tcp.get());
            }
        };
        tcpCallbacks.onError = [weak](const Error &error) {
            if (auto self = weak.lock(); self && self->callbacks.onError)
                self->callbacks.onError(error);
        };
        tcpCallbacks.onStopped = [weak] {
            if (auto self = weak.lock())
            {
                if (self->callbacks.onStopped) self->callbacks.onStopped();
                self->server.reset();
            }
        };
        server = std::make_shared<TcpServer>(loop, address, requestedPort, options.tcp,
                                             std::move(tcpCallbacks));
    }

    EventLoop &loop;
    std::string address;
    std::uint16_t requestedPort;
    Options options;
    Callbacks callbacks;
    std::shared_ptr<TcpServer> server;
    std::uint16_t boundPort{0};
    std::unordered_map<TcpConnection *, TlsStream::Ptr> tls;
    std::unordered_map<TcpConnection *, Http2Session::Ptr> sessions;
};

Http2Server::Http2Server(EventLoop &loop, std::string address, std::uint16_t port,
                         Options options, Callbacks callbacks)
    : impl_(std::make_shared<Impl>(loop, std::move(address), port, std::move(options),
                                   std::move(callbacks)))
{ impl_->initialize(); }
Http2Server::~Http2Server() { if (impl_ && impl_->server) impl_->server->stop(); }
bool Http2Server::start() noexcept { return impl_ && impl_->server && impl_->server->start(); }
bool Http2Server::stop() noexcept { return impl_ && impl_->server && impl_->server->stop(); }
std::uint16_t Http2Server::port() const noexcept { return impl_ ? impl_->boundPort : 0; }
TcpServer::State Http2Server::state() const noexcept
{ return impl_ && impl_->server ? impl_->server->state() : TcpServer::State::Stopped; }
}
