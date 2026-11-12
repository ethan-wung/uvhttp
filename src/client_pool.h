/**
 * @file        client_pool.h
 * @brief       HTTP/1 与 HTTP/2 共享的内部连接池调度实现；内部实现，不作为公共接口。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_CLIENT_POOL_INTERNAL_H
#define UVHTTP_CLIENT_POOL_INTERNAL_H

#include "uvhttp/http_client.h"
#include "uvhttp/tls_stream.h"
#ifdef UVHTTP_WITH_HTTP2
#include "uvhttp/http2_session.h"
#endif
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace uvhttp::detail
{
class ClientPool : public std::enable_shared_from_this<ClientPool>
{
public:
    struct Job
    {
        HttpRequest request;
        HttpResponse response;
        HttpClient::CompletionCallback completed;
        HttpClient::StreamCallbacks callbacks;
        HttpClient::UploadReadyCallback uploadReady;
        std::function<void(std::function<void(bool)>)> readControlReady;
#ifdef UVHTTP_WITH_HTTP2
        std::function<void(const Http2Session::Ptr &, std::int32_t)> h2Ready;
#endif
        bool h2cUpgrade{false};
        bool streaming{false};
        bool upload{false};
        bool done{false};
        bool responseStarted{false};
        unsigned retries{0};
        std::chrono::steady_clock::time_point deadline{};
    };
    ClientPool(EventLoop &, TcpConnection::Options, ConnectionPoolOptions,
               bool http2, std::size_t maxStreams, std::size_t maxBody, std::size_t maxHeaders);
    bool submit(std::string host, std::uint16_t port, SSL_CTX *, bool verify,
                std::vector<std::string> alpn, std::shared_ptr<Job>);
    void close() noexcept;
    ConnectionPoolStats stats() const;
private:
    struct Origin;
    struct Connection;
    void enqueue(std::string key, std::string host, std::uint16_t port,
                 std::shared_ptr<SSL_CTX>, bool, std::vector<std::string>, std::shared_ptr<Job>);
    void drain();
    void start(const std::shared_ptr<Origin> &);
    void ready(const std::shared_ptr<Connection> &, AsyncByteStream::Ptr);
    void dispatch(const std::shared_ptr<Connection> &, const std::shared_ptr<Job> &);
    void receive(const std::shared_ptr<Connection> &, std::string_view);
    void finish(const std::shared_ptr<Job> &, const Error &);
    void responseEnd(const std::shared_ptr<Connection> &, std::int32_t);
    void retire(const std::shared_ptr<Connection> &, const Error &);
    void maintain();
    void closeInLoop();
    void publish();
    void deferDrain();
    EventLoop &loop_;
    TcpClient tcp_;
    ConnectionPoolOptions options_;
    bool http2_;
    std::size_t maxStreams_, maxBody_, maxHeaders_;
    std::atomic<bool> closing_{false};
    std::atomic<std::size_t> outstanding_{0};
    mutable std::mutex statsMutex_;
    ConnectionPoolStats stats_;
    std::unordered_map<std::string, std::shared_ptr<Origin>> origins_;
    EventLoop::TimerId timer_{0}, shutdownHook_{0};
    bool drainPosted_{false};
};
}
#endif
