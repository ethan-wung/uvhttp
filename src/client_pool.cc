/**
 * @file        client_pool.cc
 * @brief       HTTP/1 与 HTTP/2 共享的内部连接池调度实现的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "client_pool.h"
#include "http1_parser.h"
#include "http_internal.h"
#include "uvhttp/tls_stream.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace uvhttp::detail
{
namespace
{
using Clock = std::chrono::steady_clock;

class UploadStream : public AsyncByteStream, public std::enable_shared_from_this<UploadStream>
{
public:
    explicit UploadStream(AsyncByteStream::Ptr transport) : transport_(std::move(transport)) {}
    WriteStatus writeBytes(std::string bytes) noexcept override
    {
        if (!valid_.load() || ended_.load()) return WriteStatus::Closed;
        if (isLoopThread()) return transport_->writeBytes(std::move(bytes));
        const auto size = bytes.size();
        auto queued = queued_.load();
        do {
            if (size > 4 * 1024 * 1024 - std::min<std::size_t>(queued, 4 * 1024 * 1024)) return WriteStatus::Backpressured;
        } while (!queued_.compare_exchange_weak(queued, queued + size));
        auto self = shared_from_this();
        if (!transport_->post([self, bytes = std::move(bytes), size]() mutable {
            self->queued_.fetch_sub(size);
            if (self->valid_.load() && self->transport_->writeBytes(std::move(bytes)) != WriteStatus::Accepted)
                self->transport_->closeStream();
        })) { queued_.fetch_sub(size); return WriteStatus::Closed; }
        return WriteStatus::Accepted;
    }
    bool shutdownWrite() noexcept override
    {
        if (!valid_.load()) return false;
        if (isLoopThread()) { ended_.store(true); return true; }
        auto self = shared_from_this();
        return transport_->post([self] { self->ended_.store(true); });
    }
    bool closeStream() noexcept override { invalidate(); return true; }
    bool setWritableCallback(WritableCallback callback) noexcept override
    {
        auto self = shared_from_this();
        return transport_->post([self, callback = std::move(callback)]() mutable {
            if (self->valid_.load()) self->transport_->setWritableCallback(std::move(callback));
        });
    }
    bool isLoopThread() const noexcept override { return transport_->isLoopThread(); }
    bool post(Task task) noexcept override { return transport_->post(std::move(task)); }
    bool ended() { return ended_.load(); }
    void invalidate() { valid_.store(false); }
private:
    AsyncByteStream::Ptr transport_;
    std::atomic<bool> valid_{true}, ended_{false};
    std::atomic<std::size_t> queued_{0};
};
}

struct ClientPool::Origin
{
    std::string host;
    std::uint16_t port{0};
    std::shared_ptr<SSL_CTX> context;
    bool verify{true};
    bool h2cUpgrade{false};
    std::vector<std::string> alpn;
    std::deque<std::shared_ptr<Job>> pending;
    std::vector<std::shared_ptr<Connection>> connections;
};

struct ClientPool::Connection
{
    std::weak_ptr<ClientPool> pool;
    std::weak_ptr<Origin> origin;
    TcpConnection::Ptr tcp;
    AsyncByteStream::Ptr transport;
    std::unique_ptr<Http1Parser> parser;
#ifdef UVHTTP_WITH_HTTP2
    Http2Session::Ptr h2;
    Http2Session::Callbacks upgradeCallbacks;
    bool upgrading{false}, upgradedHead{false};
#endif
    std::shared_ptr<UploadStream> upload;
    std::unordered_map<std::int32_t, std::shared_ptr<Job>> active;
    Clock::time_point started{Clock::now()}, idleSince{Clock::now()};
    std::size_t remoteMax{std::numeric_limits<std::size_t>::max()}, uses{0};
    bool ready{false}, dead{false}, draining{false}, reusable{true};
};

ClientPool::ClientPool(EventLoop &loop, TcpConnection::Options tcp, ConnectionPoolOptions options,
                       bool http2, std::size_t streams, std::size_t body, std::size_t headers)
    : loop_(loop), tcp_(loop, tcp), options_(options), http2_(http2),
      maxStreams_(streams), maxBody_(body), maxHeaders_(headers)
{
    if (!options.maxOrigins || !options.maxConnections || !options.maxConnectionsPerOrigin ||
        !options.maxPendingRequestsPerOrigin || !options.maxOutstandingRequests || !streams ||
        options.maintenanceInterval.count() <= 0 || options.connectTimeout.count() < 0 ||
        options.requestTimeout.count() < 0 || options.idleTimeout.count() < 0)
        throw std::invalid_argument("invalid connection pool limits or timeouts");
}

ConnectionPoolStats ClientPool::stats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    auto result = stats_;
    result.outstanding = outstanding_.load();
    return result;
}

bool ClientPool::submit(std::string host, std::uint16_t port, SSL_CTX *context, bool verify,
                        std::vector<std::string> alpn, std::shared_ptr<Job> job)
{
    auto count = outstanding_.load();
    for (;;)
    {
        if (closing_.load() || !loop_.isRunning() || host.empty() || !port || !job ||
            count >= options_.maxOutstandingRequests)
        {
            std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; return false;
        }
        if (outstanding_.compare_exchange_weak(count, count + 1)) break;
    }
    job->deadline = options_.requestTimeout.count()
        ? Clock::now() + options_.requestTimeout : Clock::time_point::max();
    std::shared_ptr<SSL_CTX> owned;
    if (context)
    {
        if (SSL_CTX_up_ref(context) != 1)
        { --outstanding_; std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; return false; }
        owned = std::shared_ptr<SSL_CTX>(context, SSL_CTX_free);
    }
    host = lowerAscii(host);
    std::string key = std::to_string(host.size()) + ":" + host + ":" + std::to_string(port) + ":" +
                      std::to_string(reinterpret_cast<std::uintptr_t>(context)) + (verify ? ":v" : ":n");
    if (job->h2cUpgrade) key += ":h2c-upgrade";
    for (const auto &protocol : alpn) key += ":" + std::to_string(protocol.size()) + ":" + protocol;
    auto self = shared_from_this();
    try
    {
        if (!loop_.post([self, key = std::move(key), host = std::move(host), port,
                         owned = std::move(owned), verify, alpn = std::move(alpn), job]() mutable {
            self->enqueue(std::move(key), std::move(host), port, std::move(owned), verify, std::move(alpn), job);
        })) { --outstanding_; std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; return false; }
    }
    catch (...) { --outstanding_; std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; return false; }
    std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.accepted;
    return true;
}

void ClientPool::enqueue(std::string key, std::string host, std::uint16_t port,
                         std::shared_ptr<SSL_CTX> context, bool verify,
                         std::vector<std::string> alpn, std::shared_ptr<Job> job)
{
    if (closing_.load() || !loop_.isRunning()) { finish(job, {UV_ECANCELED, "client pool closed"}); return; }
    if (!shutdownHook_)
    {
        auto weak = weak_from_this();
        shutdownHook_ = loop_.addShutdownHook([weak] { if (auto self = weak.lock()) self->closeInLoop(); });
        maintain();
    }
    for (auto iterator = origins_.begin(); iterator != origins_.end();)
    {
        if (iterator->second->connections.empty() && iterator->second->pending.empty()) iterator = origins_.erase(iterator);
        else ++iterator;
    }
    auto found = origins_.find(key);
    if (found == origins_.end())
    {
        if (origins_.size() >= options_.maxOrigins)
        {
            { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; }
            finish(job, {UV_ENOBUFS, "origin limit exceeded"}); return;
        }
        auto origin = std::make_shared<Origin>();
        origin->host = std::move(host); origin->port = port; origin->context = std::move(context);
        origin->verify = verify; origin->alpn = std::move(alpn);
        origin->h2cUpgrade = job->h2cUpgrade;
        found = origins_.emplace(std::move(key), std::move(origin)).first;
    }
    auto origin = found->second;
    if (origin->pending.size() >= options_.maxPendingRequestsPerOrigin)
    {
        { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.rejected; }
        finish(job, {UV_ENOBUFS, "origin request queue full"}); return;
    }
    origin->pending.push_back(std::move(job));
    drain();
}

void ClientPool::publish()
{
    std::size_t connections = 0, connecting = 0, idle = 0, pending = 0, active = 0;
    for (const auto &entry : origins_)
    {
        pending += entry.second->pending.size();
        for (const auto &connection : entry.second->connections)
        {
            ++connections;
            if (!connection->ready) ++connecting;
            else if (connection->active.empty() && !connection->draining) ++idle;
            active += connection->active.size();
        }
    }
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.origins = origins_.size(); stats_.connections = connections; stats_.connecting = connecting;
    stats_.idle = idle; stats_.pending = pending; stats_.active = active;
}

void ClientPool::deferDrain()
{
    if (drainPosted_ || closing_.load()) return;
    drainPosted_ = true;
    auto self = shared_from_this();
    if (!loop_.post([self] { self->drainPosted_ = false; self->drain(); })) drainPosted_ = false;
}

void ClientPool::drain()
{
    if (closing_.load()) return;
    std::vector<std::shared_ptr<Origin>> origins;
    for (const auto &entry : origins_) origins.push_back(entry.second);
    for (const auto &origin : origins)
    {
        while (!origin->pending.empty() && !closing_.load())
        {
            if (Clock::now() >= origin->pending.front()->deadline)
            {
                auto job = origin->pending.front(); origin->pending.pop_front();
                finish(job, {UV_ETIMEDOUT, "pool queue/request deadline exceeded"});
                continue;
            }
            std::shared_ptr<Connection> selected;
            for (const auto &connection : origin->connections)
                if (connection->ready && !connection->dead && !connection->draining &&
                    connection->active.size() < (http2_ ? std::min(maxStreams_, connection->remoteMax) : 1))
                { selected = connection; break; }
            if (!selected)
            {
                std::size_t total = 0;
                for (const auto &entry : origins_) total += entry.second->connections.size();
                const bool connecting = std::any_of(origin->connections.begin(), origin->connections.end(),
                    [](const auto &connection) { return !connection->ready; });
                if (!connecting && origin->connections.size() < options_.maxConnectionsPerOrigin)
                {
                    if (total >= options_.maxConnections)
                    {
                        std::shared_ptr<Connection> oldest;
                        for (const auto &entry : origins_)
                        {
                            if (entry.second == origin || !entry.second->pending.empty()) continue;
                            for (const auto &candidate : entry.second->connections)
                                if (candidate->ready && !candidate->dead && candidate->active.empty() &&
                                    (!oldest || candidate->idleSince < oldest->idleSince)) oldest = candidate;
                        }
                        if (oldest)
                        {
                            { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.capacityEvictions; }
                            retire(oldest, {});
                            --total;
                        }
                    }
                    if (total < options_.maxConnections) start(origin);
                }
                break;
            }
            auto job = origin->pending.front(); origin->pending.pop_front();
            try { dispatch(selected, job); }
            catch (...) { retire(selected, {UV_EFAULT, "request/upload callback threw"}); finish(job, {UV_EFAULT, "request submission failed"}); }
        }
    }
    publish();
}

void ClientPool::start(const std::shared_ptr<Origin> &origin)
{
    auto connection = std::make_shared<Connection>();
    connection->pool = weak_from_this(); connection->origin = origin;
    origin->connections.push_back(connection);
    { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.connectionsCreated; }
    std::weak_ptr<Connection> weak = connection;
    bool accepted;
    if (origin->context)
    {
        TlsStream::Callbacks callbacks;
        callbacks.onHandshake = [weak](const TlsStream::Ptr &stream) {
            auto connection = weak.lock();
            auto pool = connection ? connection->pool.lock() : nullptr;
            if (!pool || connection->dead) { stream->close(); return; }
            {
                const auto protocol = stream->negotiatedAlpn();
                if ((pool->http2_ && protocol != "h2") || (!pool->http2_ && !protocol.empty() && protocol != "http/1.1"))
                { pool->retire(connection, {UV_EPROTO, "unexpected TLS ALPN protocol"}); return; }
                pool->ready(connection, stream);
            }
        };
        callbacks.onData = [weak](const TlsStream::Ptr &, std::string_view bytes) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->receive(connection, bytes);
        };
        callbacks.onError = [weak](const Error &error) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                if (error.code == UV_EOF && connection->parser)
                {
                    connection->parser->finish();
                    if (connection->parser->complete()) pool->responseEnd(connection, 1);
                }
                pool->retire(connection, error);
            }
        };
        callbacks.onClosed = [weak] {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
                pool->retire(connection, {UV_EOF, "TLS connection closed"});
        };
        accepted = TlsStream::connect(loop_, tcp_, origin->host, origin->port, origin->context.get(),
                                      std::move(callbacks), origin->alpn, origin->verify, &connection->tcp);
    }
    else
    {
        TcpConnection::Callbacks callbacks;
        callbacks.onData = [weak](const auto &, std::string_view bytes) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->receive(connection, bytes);
        };
        callbacks.onPeerHalfClose = [weak](const auto &) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                if (connection->parser)
                {
                    connection->parser->finish();
                    if (connection->parser->complete()) pool->responseEnd(connection, 1);
                }
                pool->retire(connection, {UV_EOF, "peer closed connection"});
            }
        };
        callbacks.onError = [weak](const auto &, const Error &error) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->retire(connection, error);
        };
        callbacks.onClosed = [weak](const auto &) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
                pool->retire(connection, {UV_EOF, "TCP connection closed"});
        };
        accepted = tcp_.connect(origin->host, origin->port, std::move(callbacks),
            [weak](const auto &stream, const Error &error) {
                auto connection = weak.lock();
                auto pool = connection ? connection->pool.lock() : nullptr;
                if (!pool || connection->dead) { stream->close(); return; }
                if (error) pool->retire(connection, error); else pool->ready(connection, stream);
            }, &connection->tcp);
    }
    if (!accepted) retire(connection, {UV_ECANCELED, "failed to start connection"});
}

void ClientPool::ready(const std::shared_ptr<Connection> &connection, AsyncByteStream::Ptr stream)
{
    connection->transport = std::move(stream);
    std::weak_ptr<Connection> weak = connection;
#ifdef UVHTTP_WITH_HTTP2
    if (http2_)
    {
        Http2Session::Callbacks callbacks;
        callbacks.onRemoteMaxConcurrentStreams = [weak](std::size_t maximum) {
            if (auto connection = weak.lock()) connection->remoteMax = maximum;
        };
        callbacks.onRemoteSettings = [weak] {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            { connection->ready = true; connection->idleSince = Clock::now(); pool->deferDrain(); }
        };
        callbacks.onResponseHead = [weak](std::int32_t id, const HttpResponseHead &head) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                auto found = connection->active.find(id); if (found == connection->active.end()) return;
                auto job = found->second;
                std::size_t bytes = 0;
                for (const auto &field : head.headers.fields()) bytes += field.first.size() + field.second.size();
                if (bytes > pool->maxHeaders_) { pool->retire(connection, {UV_EMSGSIZE, "response headers too large"}); return; }
                job->responseStarted = true;
                static_cast<HttpResponseHead &>(job->response) = head;
                if (job->streaming && job->callbacks.onResponseHead)
                {
                    try { job->callbacks.onResponseHead(head); }
                    catch (...) { pool->retire(connection, {UV_EFAULT, "response head callback threw"}); }
                }
            }
        };
        callbacks.onResponseBody = [weak](std::int32_t id, std::string_view bytes) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                auto found = connection->active.find(id); if (found == connection->active.end()) return;
                auto job = found->second;
                if (job->streaming)
                {
                    try { if (job->callbacks.onResponseBody) job->callbacks.onResponseBody(bytes); }
                    catch (...) { pool->retire(connection, {UV_EFAULT, "response body callback threw"}); }
                }
                else if (bytes.size() > pool->maxBody_ - std::min(pool->maxBody_, job->response.body.size()))
                    pool->retire(connection, {UV_EMSGSIZE, "response body too large"});
                else job->response.body.append(bytes.data(), bytes.size());
            }
        };
        callbacks.onResponseEnd = [weak](std::int32_t id) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->responseEnd(connection, id);
        };
        callbacks.onStreamClosed = [weak](std::int32_t id, std::uint32_t code) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                auto found = connection->active.find(id); if (found == connection->active.end()) return;
                auto job = found->second; connection->active.erase(found);
                pool->finish(job, {UV_EPROTO, "HTTP/2 stream closed before response end: " + std::to_string(code)});
                connection->idleSince = Clock::now(); pool->deferDrain();
            }
        };
        callbacks.onGoAway = [weak](std::int32_t lastId) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
            {
                connection->draining = true;
                auto origin = connection->origin.lock(); if (!origin) return;
                std::vector<std::shared_ptr<Job>> refused;
                for (auto iterator = connection->active.begin(); iterator != connection->active.end();)
                {
                    if (iterator->first <= lastId) { ++iterator; continue; }
                    auto job = iterator->second; iterator = connection->active.erase(iterator);
                    if (!hasConnectionToken(job->request.headers, "close") && !job->upload && !job->responseStarted && job->retries++ == 0 &&
                        origin->pending.size() < pool->options_.maxPendingRequestsPerOrigin)
                    {
                        origin->pending.push_back(job);
                        std::lock_guard<std::mutex> lock(pool->statsMutex_); ++pool->stats_.goawayRetries;
                    }
                    else refused.push_back(job);
                }
                for (const auto &job : refused) pool->finish(job, {UV_ECANCELED, "GOAWAY request cannot be replayed safely"});
                if (connection->active.empty()) pool->retire(connection, {});
                pool->deferDrain();
            }
        };
        callbacks.onError = [weak](const Error &error) {
            if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->retire(connection, error);
        };
        auto origin=connection->origin.lock();
        if(origin && origin->h2cUpgrade) {
            if(origin->pending.empty()) { retire(connection,{}); return; }
            auto job=origin->pending.front(); origin->pending.pop_front();
            if(job->upload) { finish(job,{UV_ENOTSUP,"h2c upgrade requires a buffered initial request"}); retire(connection,{}); return; }
            connection->active.emplace(1,job); connection->uses=1;
            connection->upgrading=true; connection->upgradedHead=job->request.method=="HEAD";
            connection->upgradeCallbacks=std::move(callbacks);
            auto request=job->request;
            request.version="HTTP/1.1";
            request.headers.set("Connection","Upgrade, HTTP2-Settings");
            request.headers.set("Upgrade","h2c");
            request.headers.set("HTTP2-Settings","AAIAAAAA");
            request.headers.set("Content-Length",std::to_string(request.body.size()));
            Http1Parser::Callbacks handlers;
            handlers.onHead=[weak](const Http1MessageHead &message) {
                auto connection=weak.lock(); auto pool=connection ? connection->pool.lock() : nullptr;
                if(!pool) return;
                const auto &head=message.responseHead;
                if(head.status>=100 && head.status<200 && head.status!=101) return;
                if(head.status!=101 || lowerAscii(head.headers.get("Upgrade"))!="h2c" || !hasConnectionToken(head.headers,"upgrade"))
                    pool->retire(connection,{UV_EPROTO,"server refused or malformed h2c upgrade"});
            };
            handlers.onError=[weak](const Error &error) { if(auto connection=weak.lock()) if(auto pool=connection->pool.lock()) pool->retire(connection,error); };
            connection->parser=std::make_unique<Http1Parser>(Http1Parser::Kind::Response,maxHeaders_,std::move(handlers));
            std::string bytes;
            if(!serializeRequestHead(request,origin->host,origin->port,false,true,bytes)) { retire(connection,{UV_EINVAL,"invalid upgrade request"}); return; }
            bytes+=request.body;
            if(connection->transport->writeBytes(std::move(bytes))!=AsyncByteStream::WriteStatus::Accepted)
                retire(connection,{UV_ENOBUFS,"upgrade request rejected by transport"});
            return;
        }
        connection->h2 = Http2Session::createClient(connection->transport, std::move(callbacks), maxHeaders_, maxBody_);
        if (!connection->h2) retire(connection, {UV_EPROTO, "cannot create HTTP/2 session"});
        return;
    }
#endif
    connection->ready = true;
    connection->idleSince = Clock::now();
    deferDrain();
}

void ClientPool::dispatch(const std::shared_ptr<Connection> &connection, const std::shared_ptr<Job> &job)
{
    auto origin = connection->origin.lock(); if (!origin) { finish(job, {UV_ECANCELED, "origin closed"}); return; }
    if (connection->uses++) { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.reused; }
    auto authority = [&] {
        auto value = origin->host.find(':') == std::string::npos ? origin->host : "[" + origin->host + "]";
        const auto defaultPort = origin->context ? 443 : 80;
        if (origin->port != defaultPort) value += ":" + std::to_string(origin->port);
        return value;
    };
    if (job->request.headers.get("Host").empty()) job->request.headers.set("Host", authority());
    if (job->readControlReady) {
        std::weak_ptr<Connection> weakConnection=connection;
        std::weak_ptr<Job> weakJob=job;
        job->readControlReady([weakConnection,weakJob](bool paused) {
            auto connection=weakConnection.lock(); auto pool=connection ? connection->pool.lock() : nullptr;
            if(!pool) return;
            auto action=[weakConnection,weakJob,paused] {
                auto connection=weakConnection.lock(); auto job=weakJob.lock();
                if(!connection || connection->dead || !job || job->done || !connection->tcp) return;
                if(paused) connection->tcp->pauseRead(); else connection->tcp->resumeRead();
            };
            if(pool->loop_.isLoopThread()) action(); else pool->loop_.post(std::move(action));
        });
    }
#ifdef UVHTTP_WITH_HTTP2
    if (http2_)
    {
        job->request.headers.set(":scheme", origin->context ? "https" : "http");
        auto id = job->upload ? connection->h2->requestStream(job->request) : connection->h2->request(job->request);
        if (id < 0) { finish(job, {UV_EPROTO, "HTTP/2 request submission failed"}); return; }
        connection->active.emplace(id, job);
        if (job->upload)
        {
            if (job->h2Ready) job->h2Ready(connection->h2, id);
            else connection->h2->endRequestData(id);
        }
        return;
    }
#endif
    connection->active.emplace(1, job);
    connection->reusable = !hasConnectionToken(job->request.headers, "close");
    std::weak_ptr<Connection> weak = connection;
    Http1Parser::Callbacks callbacks;
    callbacks.onHead = [weak, job](const Http1MessageHead &message) {
        auto connection = weak.lock(); if (!connection || connection->dead) return;
        const auto &head = message.responseHead;
        if (head.status >= 100 && head.status < 200 && head.status != 101) return;
        job->responseStarted = true;
        static_cast<HttpResponseHead &>(job->response) = head;
        connection->reusable = connection->reusable && head.status != 101 &&
            !hasConnectionToken(head.headers, "close") &&
            (head.version == "HTTP/1.1" || hasConnectionToken(head.headers, "keep-alive")) &&
            (job->request.method == "HEAD" || head.status == 204 || head.status == 304 ||
             head.headers.contains("Content-Length") || head.headers.contains("Transfer-Encoding"));
        if (job->streaming && job->callbacks.onResponseHead) job->callbacks.onResponseHead(head);
    };
    callbacks.onBody = [weak, job](std::string_view bytes) {
        auto connection = weak.lock(); if (!connection || connection->dead) return;
        auto pool = connection->pool.lock(); if (!pool) return;
        if (job->streaming) { if (job->callbacks.onResponseBody) job->callbacks.onResponseBody(bytes); }
        else if (bytes.size() > pool->maxBody_ - std::min(pool->maxBody_, job->response.body.size()))
            pool->retire(connection, {UV_EMSGSIZE, "response body too large"});
        else job->response.body.append(bytes.data(), bytes.size());
    };
    callbacks.onComplete = [weak] {
        if (auto connection = weak.lock()) if (auto pool = connection->pool.lock())
        {
            if (!pool->loop_.post([pool, connection] { pool->responseEnd(connection, 1); }))
                pool->retire(connection, {UV_ECANCELED, "event loop stopped before response completion"});
        }
    };
    callbacks.onError = [weak](const Error &error) {
        if (auto connection = weak.lock()) if (auto pool = connection->pool.lock()) pool->retire(connection, error);
    };
    connection->parser = std::make_unique<Http1Parser>(Http1Parser::Kind::Response, maxHeaders_, std::move(callbacks));
    connection->parser->setResponseToHead(job->request.method == "HEAD");
    job->request.headers.remove("Transfer-Encoding");
    if (job->upload) job->request.headers.remove("Content-Length");
    else job->request.headers.set("Content-Length", std::to_string(job->request.body.size()));
    std::string wire;
    if (!serializeRequestHead(job->request, origin->host, origin->port, job->upload, connection->reusable, wire))
    { retire(connection, {UV_EINVAL, "invalid HTTP request"}); return; }
    if (!job->upload) wire += job->request.body;
    if (connection->transport->writeBytes(std::move(wire)) != AsyncByteStream::WriteStatus::Accepted)
    { retire(connection, {UV_ENOBUFS, "HTTP request write rejected"}); return; }
    if (job->upload)
    {
        connection->upload = std::make_shared<UploadStream>(connection->transport);
        auto writer = HttpRequestBodyWriter::fromStream(connection->upload);
        if (job->uploadReady) job->uploadReady(writer); else writer.end();
    }
}

void ClientPool::receive(const std::shared_ptr<Connection> &connection, std::string_view bytes)
{
    if (connection->dead) return;
    try
    {
#ifdef UVHTTP_WITH_HTTP2
        if (connection->h2) { connection->h2->receive(bytes); return; }
#endif
        if (!connection->parser || connection->active.empty())
        { retire(connection, {UV_EPROTO, "unsolicited HTTP response bytes"}); return; }
        connection->parser->feed(bytes);
#ifdef UVHTTP_WITH_HTTP2
        if(connection->upgrading && !connection->dead && connection->parser->complete()) {
            auto buffered=connection->parser->takeBuffered(); connection->parser.reset();
            connection->upgrading=false;
            const std::string settings("\0\2\0\0\0\0",6);
            connection->h2=Http2Session::createUpgradeClient(connection->transport,std::move(connection->upgradeCallbacks),
                settings,connection->upgradedHead,maxHeaders_,maxBody_);
            if(!connection->h2) { retire(connection,{UV_EPROTO,"cannot initialize upgraded HTTP/2"}); return; }
            if(!buffered.empty()) connection->h2->receive(buffered);
            return;
        }
#endif
        if (connection->parser->complete() && connection->parser->hasBufferedInput())
            retire(connection, {UV_EPROTO, "unexpected bytes after HTTP response"});
    }
    catch (...) { retire(connection, {UV_EFAULT, "HTTP response callback threw"}); }
}

void ClientPool::responseEnd(const std::shared_ptr<Connection> &connection, std::int32_t id)
{
    auto found = connection->active.find(id); if (found == connection->active.end()) return;
    auto job = found->second; connection->active.erase(found);
    if (job->readControlReady && connection->tcp) connection->tcp->resumeRead();
    connection->idleSince = Clock::now();
#ifdef UVHTTP_WITH_HTTP2
    if (http2_ && job->upload && connection->h2) connection->h2->resetStream(id);
#endif
    if (connection->upload)
    {
        connection->reusable = connection->reusable && connection->upload->ended();
        connection->upload->invalidate(); connection->upload.reset();
    }
    if (!http2_ && connection->transport) connection->transport->setWritableCallback({});
    if ((!http2_ && !connection->reusable) || (connection->draining && connection->active.empty()))
        retire(connection, {});
    publish();
    finish(job, {});
    deferDrain();
}

void ClientPool::finish(const std::shared_ptr<Job> &job, const Error &error)
{
    if (job->done) return;
    job->done = true; --outstanding_;
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        if (error) ++stats_.failed; else ++stats_.completed;
        if (error.code == UV_ETIMEDOUT) ++stats_.timedOut;
    }
    try
    {
        if (!job->streaming) { if (job->completed) job->completed(std::move(job->response), error); }
        else if (error) { if (job->callbacks.onError) job->callbacks.onError(error); }
        else if (job->callbacks.onComplete) job->callbacks.onComplete();
    }
    catch (...)
    {
        auto failure = std::current_exception();
        loop_.post([failure] { std::rethrow_exception(failure); });
    }
}

void ClientPool::retire(const std::shared_ptr<Connection> &connection, const Error &error)
{
    if (connection->dead) return;
    connection->dead = true;
    if (connection->upload) connection->upload->invalidate();
    auto origin = connection->origin.lock();
    if (origin)
    {
        auto &connections = origin->connections;
        connections.erase(std::remove(connections.begin(), connections.end(), connection), connections.end());
    }
    auto active = std::move(connection->active); connection->active.clear();
    if (connection->transport)
    {
        if (std::dynamic_pointer_cast<TlsStream>(connection->transport)) connection->transport->closeStream();
        else if (connection->tcp) connection->tcp->close();
        else connection->transport->closeStream();
    }
    else if (connection->tcp) connection->tcp->close();
    const Error failure = error ? error : Error{UV_ECANCELED, "connection retired"};
    for (auto &entry : active) finish(entry.second, failure);
    if (origin && !connection->ready && origin->connections.empty())
    {
        auto pending = std::move(origin->pending); origin->pending.clear();
        const Error pendingFailure = error ? error : Error{UV_ECONNABORTED, "connection failed"};
        for (auto &job : pending) finish(job, pendingFailure);
    }
    publish(); deferDrain();
}

void ClientPool::maintain()
{
    timer_ = 0;
    if (closing_.load()) return;
    auto now = Clock::now();
    std::vector<std::shared_ptr<Connection>> connections;
    std::vector<std::shared_ptr<Job>> expired;
    for (auto &entry : origins_)
    {
        auto &pending = entry.second->pending;
        for (auto iterator = pending.begin(); iterator != pending.end();)
        {
            if ((*iterator)->deadline != Clock::time_point::max() && now >= (*iterator)->deadline)
            { expired.push_back(*iterator); iterator = pending.erase(iterator); }
            else ++iterator;
        }
        connections.insert(connections.end(), entry.second->connections.begin(), entry.second->connections.end());
    }
    for (const auto &job : expired) finish(job, {UV_ETIMEDOUT, "pool queue/request deadline exceeded"});
    for (const auto &connection : connections)
    {
        if (connection->dead) continue;
        bool timeout = !connection->ready && options_.connectTimeout.count() && now - connection->started >= options_.connectTimeout;
        std::vector<std::int32_t> expiredStreams;
        for (const auto &entry : connection->active)
            if (entry.second->deadline != Clock::time_point::max() && now >= entry.second->deadline)
                expiredStreams.push_back(entry.first);
#ifdef UVHTTP_WITH_HTTP2
        if (http2_ && connection->h2)
        {
            for (const auto id : expiredStreams)
            {
                auto found = connection->active.find(id); if (found == connection->active.end()) continue;
                auto job = found->second; connection->active.erase(found);
                connection->h2->resetStream(id);
                finish(job, {UV_ETIMEDOUT, "HTTP/2 stream deadline exceeded"});
                connection->idleSince = now;
            }
        }
        else
#endif
        timeout = timeout || !expiredStreams.empty();
        if (timeout) { retire(connection, {UV_ETIMEDOUT, "connection/request deadline exceeded"}); continue; }
        if (connection->ready && connection->active.empty() && options_.idleTimeout.count() &&
            now - connection->idleSince >= options_.idleTimeout)
        {
            { std::lock_guard<std::mutex> lock(statsMutex_); ++stats_.idleEvictions; }
            retire(connection, {});
        }
    }
    for (auto iterator = origins_.begin(); iterator != origins_.end();)
        if (iterator->second->connections.empty() && iterator->second->pending.empty()) iterator = origins_.erase(iterator);
        else ++iterator;
    drain();
    if (!closing_.load() && loop_.isRunning())
    {
        auto weak = weak_from_this();
        timer_ = loop_.scheduleAfter(options_.maintenanceInterval, [weak] { if (auto self = weak.lock()) self->maintain(); });
    }
}

void ClientPool::close() noexcept
{
    if (closing_.exchange(true)) return;
    auto self = shared_from_this();
    if (loop_.isLoopThread()) closeInLoop();
    else loop_.post([self] { self->closeInLoop(); });
}

void ClientPool::closeInLoop()
{
    closing_.store(true);
    loop_.cancelTimer(timer_); timer_ = 0;
    loop_.removeShutdownHook(shutdownHook_); shutdownHook_ = 0;
    auto origins = std::move(origins_); origins_.clear();
    for (auto &entry : origins)
    {
        auto pending = std::move(entry.second->pending); entry.second->pending.clear();
        auto connections = entry.second->connections;
        for (auto &job : pending) finish(job, {UV_ECANCELED, "client pool closed"});
        for (const auto &connection : connections) retire(connection, {UV_ECANCELED, "client pool closed"});
    }
    publish();
}
}
