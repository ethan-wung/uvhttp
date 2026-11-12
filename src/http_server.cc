/**
 * @file        http_server.cc
 * @brief       HTTP/1.x 异步服务端、流式请求、TLS 和 WebSocket 升级的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http_server.h"

#include <memory>
#include <unordered_map>
#include <utility>

#include "http1_parser.h"
#include "h2c_internal.h"
#include <stdexcept>
#include "uvhttp/tls_stream.h"

namespace uvhttp
{

struct HttpServer::Impl : std::enable_shared_from_this<HttpServer::Impl>
{
    struct Session : std::enable_shared_from_this<Session>
    {
        Session(std::weak_ptr<Impl> ownerValue,
                AsyncByteStream::Ptr streamValue,
                std::size_t maxBody,
                std::size_t maxHeaders)
            : owner(std::move(ownerValue)), stream(std::move(streamValue)),
              parser(detail::Http1Parser::Kind::Request, maxHeaders, makeParserCallbacks())
        {
            bufferedBodyLimit = maxBody;
        }

        detail::Http1Parser::Callbacks makeParserCallbacks()
        {
            detail::Http1Parser::Callbacks result;
            result.onHead = [this](const detail::Http1MessageHead &message) {
                requestHead = message.requestHead;
                keepAlive = requestHead.version == "HTTP/1.1"
                                ? !detail::hasConnectionToken(requestHead.headers, "close")
                                : detail::hasConnectionToken(requestHead.headers, "keep-alive");
                auto weak = weak_from_this();
                responseWriter = HttpResponseWriter::fromStream(
                    stream, requestHead.method == "HEAD", keepAlive,
                    [weak] {
                        if (auto session = weak.lock())
                            session->stream->post([weak] {
                                if (auto current = weak.lock()) current->responseFinished();
                            });
                    });
#ifdef UVHTTP_WITH_HTTP2
                auto serverOptions = owner.lock();
                if (serverOptions && serverOptions->options.enableHttp2 && serverOptions->options.enableH2cUpgrade &&
                    !serverOptions->options.tlsContext && detail::lowerAscii(requestHead.headers.get("Upgrade")) == "h2c") {
                    const auto values = requestHead.headers.getAll("HTTP2-Settings");
                    if (requestHead.version != "HTTP/1.1" || requestHead.headers.getAll("Upgrade").size()!=1 || requestHead.headers.getAll("Host").size()!=1 ||
                        requestHead.headers.get("Host").empty() || values.size()!=1 ||
                        !detail::hasConnectionToken(requestHead.headers,"upgrade") ||
                        !detail::hasConnectionToken(requestHead.headers,"http2-settings") ||
                        !detail::decodeSettings(values.front(),upgradeSettings)) {
                        done=true; auto writer=HttpResponseWriter::fromStream(stream,false,false);
                        HttpResponse bad; bad.status=400; writer.send(std::move(bad)); return;
                    }
                    h2cPending=true;
                }
#endif
                if (isWebSocketUpgrade(requestHead.headers)) {
                    if (requestHead.headers.get("Sec-WebSocket-Key").empty() || requestHead.headers.get("Sec-WebSocket-Version") != "13") {
                        done = true; keepAlive = false; responseWriter = HttpResponseWriter::fromStream(stream, false, false); HttpResponse bad; bad.status = 400; responseWriter.send(std::move(bad)); return;
                    }

                    upgraded = true; keepAlive = false; requestDispatched = true;
                    responseWriter = HttpResponseWriter::fromStream(stream, false, true, [] {});
                    responseWriter.send(websocketHandshakeResponse(requestHead));
                    websocket = WebSocketConnection::create(stream, true, {}, owner.lock() ? owner.lock()->options.maxWebSocketPayloadBytes : 64 * 1024 * 1024);
                    if (auto server = owner.lock(); server && server->callbacks.onWebSocket) server->callbacks.onWebSocket(requestHead, websocket);
                    return;
                }
                if (requestHead.version == "HTTP/1.1" &&
                    (requestHead.headers.getAll("Host").size() != 1 || requestHead.headers.get("Host").empty()))
                {
                    done = true;
                    keepAlive = false;
                    responseWriter = HttpResponseWriter::fromStream(stream, false, false);
                    HttpResponse response;
                    response.status = 400;
                    responseWriter.send(std::move(response));
                    return;
                }
                const auto expectations = requestHead.headers.getAll("Expect");
                if (!expectations.empty())
                {
                    bool supported = expectations.size() == 1 &&
                                     detail::lowerAscii(detail::trimOws(expectations.front())) == "100-continue";
                    if (!supported)
                    {
                        done = true;
                        keepAlive = false;
                        responseWriter = HttpResponseWriter::fromStream(stream, false, false);
                        HttpResponse rejected;
                        rejected.status = 417;
                        responseWriter.send(std::move(rejected));
                        return;
                    }
                    if (stream->writeBytes("HTTP/1.1 100 Continue\r\n\r\n") != AsyncByteStream::WriteStatus::Accepted)
                    {
                        done = true;
                        stream->closeStream();
                        return;
                    }
                }
                streaming = false;
#ifdef UVHTTP_WITH_HTTP2
                if (h2cPending) return;
#endif
                if (auto server = owner.lock())
                {
                    if (server->callbacks.onRequestHead)
                        streaming = server->callbacks.onRequestHead(requestHead, responseWriter);
                }
            };
            result.onBody = [this](std::string_view bytes) {
                if (done) return;
                if (streaming)
                {
                    if (auto server = owner.lock(); server && server->callbacks.onRequestBody)
                        server->callbacks.onRequestBody(requestHead, bytes);
                    return;
                }
                if (bytes.size() > bufferedBodyLimit || request.body.size() > bufferedBodyLimit - bytes.size())
                {
                    done = true;
                    keepAlive = false;
                    responseWriter = HttpResponseWriter::fromStream(stream, false, false);
                    HttpResponse response;
                    response.status = 413;
                    responseWriter.send(std::move(response));
                    return;
                }
                request.body.append(bytes.data(), bytes.size());
            };
            result.onComplete = [this] {
                if (upgraded) return;
                if (done) return;
                requestDispatched = true;
#ifdef UVHTTP_WITH_HTTP2
                if (h2cPending) {
                    static_cast<HttpRequestHead &>(request)=requestHead;
                    if (stream->writeBytes("HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n") != AsyncByteStream::WriteStatus::Accepted) {
                        done=true; stream->closeStream(); return;
                    }
                    if (auto server=owner.lock()) {
                        h2=server->makeHttp2(stream);
                        if (!h2 || !h2->upgradeServer(upgradeSettings,std::move(request))) { done=true; stream->closeStream(); }
                    }
                    return;
                }
#endif
                if (auto server = owner.lock())
                {
                    if (streaming)
                    {
                        if (server->callbacks.onRequestEnd)
                            server->callbacks.onRequestEnd(requestHead, responseWriter);
                    }
                    else if (server->callbacks.onRequest)
                    {
                        static_cast<HttpRequestHead &>(request) = requestHead;
                        server->callbacks.onRequest(request, responseWriter);
                    }
                    else
                    {
                        HttpResponse response;
                        response.status = 404;
                        responseWriter.send(std::move(response));
                    }
                }
            };
            result.onError = [this](const Error &error) {
                if (done) return;
                done = true;
                if (auto server = owner.lock(); server && server->callbacks.onError)
                    server->callbacks.onError(error);
                if (stream)
                {
                    auto writer = responseWriter ? responseWriter : HttpResponseWriter::fromStream(stream);
                    HttpResponse response;
                    response.status = 400;
                    writer.send(std::move(response));
                }
                else if (stream)
                {
                    stream->closeStream();
                }
            };
            return result;
        }

        void responseFinished()
        {
            if (!keepAlive)
            {
                if (stream) stream->shutdownWrite();
                return;
            }
            request = HttpRequest{};
            requestHead = HttpRequestHead{};
            responseWriter = HttpResponseWriter{};
            streaming = false;
            requestDispatched = false;
            done = false;
            parser.resume();
        }

        std::weak_ptr<Impl> owner;
        AsyncByteStream::Ptr stream;
        detail::Http1Parser parser;
        void feedBytes(std::string_view bytes) {
#ifdef UVHTTP_WITH_HTTP2
            if (h2) { h2->receive(bytes); return; }
#endif
            if (websocket) { websocket->feed(bytes); return; }
            parser.feed(bytes);
#ifdef UVHTTP_WITH_HTTP2
            if (h2) { auto buffered=parser.takeBuffered(); if (!buffered.empty()) h2->receive(buffered); return; }
#endif
            if (websocket) { auto buffered=parser.takeBuffered(); if (!buffered.empty()) websocket->feed(buffered); }
        }
#ifdef UVHTTP_WITH_HTTP2
        Http2Session::Ptr h2;
        bool h2cPending{false};
        std::string upgradeSettings;
#endif
        HttpRequest request;
        HttpRequestHead requestHead;
        HttpResponseWriter responseWriter;
        std::size_t bufferedBodyLimit{0};
        bool streaming{false};
        bool keepAlive{false};
        bool requestDispatched{false};
        bool done{false};
        bool upgraded{false};
        WebSocketConnection::Ptr websocket;
    };

    Impl(EventLoop &loopValue,
         std::string addressValue,
         std::uint16_t portValue,
         Options optionsValue,
         Callbacks callbacksValue)
        : loop(loopValue), address(std::move(addressValue)), requestedPort(portValue),
          options(std::move(optionsValue)), callbacks(std::move(callbacksValue)) {}

#ifdef UVHTTP_WITH_HTTP2
    Http2Session::Ptr makeHttp2(const AsyncByteStream::Ptr &stream) {
        auto handlers=callbacks.http2;
        if (!handlers.onError) handlers.onError=callbacks.onError;
        return Http2Session::createServer(stream,std::move(handlers),options.maxHttp2ConcurrentStreams,
                                          options.maxBufferedRequestBodyBytes,options.maxHeaderBytes);
    }
    void receivePlain(const TcpConnection::Ptr &connection,std::string_view bytes) {
        const auto key=connection.get();
        auto h2=h2Sessions.find(key);
        if(h2!=h2Sessions.end()) { h2->second->receive(bytes); return; }
        auto pending=detecting.find(key);
        if(pending==detecting.end()) {
            auto found=sessions.find(key);
            if(found!=sessions.end() && !found->second->done) found->second->feedBytes(bytes);
            return;
        }
        const std::string_view preface="PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        auto &buffer=pending->second;
        const auto take=std::min(bytes.size(),preface.size()-buffer.size());
        buffer.append(bytes.data(),take);
        if(preface.substr(0,buffer.size())==buffer && buffer.size()<preface.size()) return;
        const bool isH2=buffer==preface;
        std::string first=std::move(buffer); detecting.erase(pending);
        auto timer=detectionTimers.find(key);
        if(timer!=detectionTimers.end()) { loop.cancelTimer(timer->second); detectionTimers.erase(timer); }
        if(isH2) {
            auto session=makeHttp2(connection);
            if(!session) { connection->close(); return; }
            h2Sessions[key]=session; session->receive(first);
            if(bytes.size()>take) session->receive(bytes.substr(take));
        } else {
            auto session=std::make_shared<Session>(weak_from_this(),connection,options.maxBufferedRequestBodyBytes,options.maxHeaderBytes);
            sessions[key]=session; session->feedBytes(first);
            if(bytes.size()>take) session->feedBytes(bytes.substr(take));
        }
    }
#endif

    void initialize()
    {
        std::weak_ptr<Impl> weak = shared_from_this();
        TcpServer::Callbacks tcpCallbacks;
        tcpCallbacks.onListening = [weak](std::uint16_t value) {
            if (auto self = weak.lock())
            {
                self->boundPort = value;
                if (self->callbacks.onListening) self->callbacks.onListening(value);
            }
        };
        tcpCallbacks.onConnection = [weak](const TcpConnection::Ptr &connection) {
            if (auto self = weak.lock())
            {
                const auto key = connection.get();
                if (!self->options.tlsContext)
                {
#ifdef UVHTTP_WITH_HTTP2
                    if (self->options.enableHttp2) {
                        self->detecting.emplace(key,std::string{});
                        std::weak_ptr<TcpConnection> weakConnection=connection;
                        self->detectionTimers[key]=self->loop.scheduleAfter(self->options.protocolDetectionTimeout,[weak,weakConnection,key] {
                            auto owner=weak.lock(); auto tcp=weakConnection.lock();
                            if(owner && tcp) { owner->detectionTimers.erase(key); if(owner->detecting.erase(key)) tcp->close(); }
                        });
                        return;
                    }
#endif
                    self->sessions.emplace(key, std::make_shared<Session>(weak, connection,
                        self->options.maxBufferedRequestBodyBytes, self->options.maxHeaderBytes));
                    return;
                }
                TlsStream::Callbacks tlsCallbacks;
                tlsCallbacks.onHandshake = [weak, key](const TlsStream::Ptr &tls) {
                    if (auto owner = weak.lock()) {
#ifdef UVHTTP_WITH_HTTP2
                        if (tls->negotiatedAlpn()=="h2" && owner->options.enableHttp2) {
                            auto session=owner->makeHttp2(tls);
                            if(session) owner->h2Sessions[key]=session; else tls->closeStream();
                            return;
                        }
#endif
                        if (!tls->negotiatedAlpn().empty() && tls->negotiatedAlpn()!="http/1.1") { tls->closeStream(); return; }
                        owner->sessions[key] = std::make_shared<Session>(weak, tls,
                            owner->options.maxBufferedRequestBodyBytes, owner->options.maxHeaderBytes);
                    }
                };
                tlsCallbacks.onData = [weak, key](const TlsStream::Ptr &, std::string_view bytes) {
                    if (auto owner = weak.lock()) {
#ifdef UVHTTP_WITH_HTTP2
                        auto h2=owner->h2Sessions.find(key);
                        if(h2!=owner->h2Sessions.end()) { h2->second->receive(bytes); return; }
#endif
                        auto found = owner->sessions.find(key);
                        if (found != owner->sessions.end() && !found->second->done)
                            found->second->feedBytes(bytes);
                    }
                };
                tlsCallbacks.onError = [weak](const Error &error) {
                    if (auto owner = weak.lock(); owner && owner->callbacks.onError)
                        owner->callbacks.onError(error);
                };
                auto tls = TlsStream::accept(connection, self->options.tlsContext,
                    std::move(tlsCallbacks), self->options.tlsAlpnProtocols);
                if (tls) self->tlsStreams.emplace(key, std::move(tls));
            }
        };
        tcpCallbacks.onData = [weak](const TcpConnection::Ptr &connection, std::string_view bytes) {
            if (auto self = weak.lock())
            {
                const auto tls = self->tlsStreams.find(connection.get());
                if (tls != self->tlsStreams.end()) {
                    tls->second->feedEncrypted(bytes);
                    return;
                }
#ifdef UVHTTP_WITH_HTTP2
                if(self->options.enableHttp2) { self->receivePlain(connection,bytes); return; }
#endif
                const auto found = self->sessions.find(connection.get());
                if (found != self->sessions.end() && !found->second->done)
                    found->second->feedBytes(bytes);
            }
        };
        tcpCallbacks.onPeerHalfClose = [weak](const TcpConnection::Ptr &connection) {
            if (auto self = weak.lock())
            {
                const auto found = self->sessions.find(connection.get());
                if (found != self->sessions.end()) found->second->parser.finish();
            }
        };
        tcpCallbacks.onConnectionClosed = [weak](const TcpConnection::Ptr &connection) {
            if (auto self = weak.lock()) {
                self->sessions.erase(connection.get()); self->tlsStreams.erase(connection.get());
#ifdef UVHTTP_WITH_HTTP2
                self->h2Sessions.erase(connection.get()); self->detecting.erase(connection.get());
                auto timer=self->detectionTimers.find(connection.get());
                if(timer!=self->detectionTimers.end()) { self->loop.cancelTimer(timer->second); self->detectionTimers.erase(timer); }
#endif
            }
        };
        tcpCallbacks.onError = [weak](const Error &error) {
            if (auto self = weak.lock(); self && self->callbacks.onError)
                self->callbacks.onError(error);
        };
        tcpCallbacks.onConnectionError = [weak](const TcpConnection::Ptr &, const Error &error) {
            if (auto self = weak.lock(); self && self->callbacks.onError)
                self->callbacks.onError(error);
        };
        tcpCallbacks.onStopped = [self = shared_from_this()] {
            if (self->callbacks.onStopped) self->callbacks.onStopped();
            self->server.reset();
        };
        server = std::make_shared<TcpServer>(loop, address, requestedPort,
                                             options.tcp, std::move(tcpCallbacks));
    }

    EventLoop &loop;
    std::string address;
    std::uint16_t requestedPort;
    Options options;
    Callbacks callbacks;
    std::shared_ptr<TcpServer> server;
    std::unordered_map<TcpConnection *, std::shared_ptr<Session>> sessions;
    std::unordered_map<TcpConnection *, TlsStream::Ptr> tlsStreams;
#ifdef UVHTTP_WITH_HTTP2
    std::unordered_map<TcpConnection *, Http2Session::Ptr> h2Sessions;
    std::unordered_map<TcpConnection *, std::string> detecting;
    std::unordered_map<TcpConnection *, EventLoop::TimerId> detectionTimers;
#endif
    std::uint16_t boundPort{0};
};

HttpServer::HttpServer(EventLoop &loop, std::string address, std::uint16_t port, Callbacks callbacks)
    : HttpServer(loop, std::move(address), port, Options{}, std::move(callbacks)) {}

HttpServer::HttpServer(EventLoop &loop,
                       std::string address,
                       std::uint16_t port,
                       Options options,
                       Callbacks callbacks)
    : impl_(std::make_shared<Impl>(loop, std::move(address), port,
                                   std::move(options), std::move(callbacks)))
{
    if (impl_->options.protocolDetectionTimeout.count()<=0) throw std::invalid_argument("invalid protocol detection timeout");
#ifdef UVHTTP_WITH_HTTP2
    if (impl_->options.enableHttp2) impl_->options.tlsAlpnProtocols={"h2","http/1.1"};
#endif
    impl_->initialize();
}

HttpServer::~HttpServer()
{
    if (impl_ && impl_->server) impl_->server->stop();
}

bool HttpServer::start() noexcept
{
    return impl_ && impl_->server && impl_->server->start();
}

bool HttpServer::stop() noexcept
{
    return impl_ && impl_->server && impl_->server->stop();
}

std::uint16_t HttpServer::port() const noexcept
{
    return impl_ ? impl_->boundPort : 0;
}

TcpServer::State HttpServer::state() const noexcept
{
    return impl_ && impl_->server ? impl_->server->state() : TcpServer::State::Stopped;
}

}  // namespace uvhttp








