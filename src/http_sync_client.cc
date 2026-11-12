/**
 * @file        http_sync_client.cc
 * @brief       HTTP/HTTPS 阻塞客户端及可复用连接池的同步会话的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http_sync_client.h"

#include <memory>
#include <future>
#include <mutex>
#include <utility>

#include "uvhttp/event_loop.h"
#include "uvhttp/http_client.h"
#include "uvhttp/event_loop_thread.h"

namespace uvhttp
{
namespace
{
HttpResponse performRequest(std::string host, std::uint16_t port, SSL_CTX *context,
                            HttpRequest request, Error &error,
                            std::vector<std::string> alpnProtocols, bool verifyPeer)
{
    EventLoop loop;
    HttpClient client(loop);
    HttpResponse response;
    bool completed = false;
    auto completion = [&](HttpResponse value, const Error &result) {
        response = std::move(value);
        error = result;
        completed = true;
        loop.stop();
    };
    const bool accepted = context
        ? client.requestTls(std::move(host), port, context, std::move(request), completion,
                           std::move(alpnProtocols), verifyPeer)
        : client.request(std::move(host), port, std::move(request), completion);
    if (!accepted)
    {
        error = Error{UV_ECANCELED, "HTTP request could not be queued"};
        loop.stop();
    }
    loop.run();
    if (!completed && !error) error = Error{UV_ECANCELED, "HTTP event loop stopped before completion"};
    return response;
}
}

HttpResponse HttpSyncClient::request(std::string host, std::uint16_t port,
                                     HttpRequest requestValue, Error &error)
{
    error = {};
    return performRequest(std::move(host), port, nullptr, std::move(requestValue),
                          error, {}, true);
}

HttpResponse HttpSyncClient::requestTls(std::string host, std::uint16_t port,
                                        SSL_CTX *context, HttpRequest requestValue,
                                        Error &error, std::vector<std::string> alpnProtocols,
                                        bool verifyPeer)
{
    error = {};
    if (!context)
    {
        error = Error{UV_EINVAL, "TLS context is required"};
        return {};
    }
    return performRequest(std::move(host), port, context, std::move(requestValue),
                          error, std::move(alpnProtocols), verifyPeer);
}

struct HttpSyncClientSession::Impl
{
    EventLoopThread thread;
    std::unique_ptr<HttpClient> client;

    explicit Impl(HttpClient::Options options)
    {
        auto &loop = thread.start();
        client = std::make_unique<HttpClient>(loop, std::move(options));
    }

    ~Impl()
    {
        client.reset();
        thread.stop();
        thread.join();
    }

    HttpResponse perform(std::string host, std::uint16_t port, SSL_CTX *context,
                         HttpRequest request, Error &error,
                         std::vector<std::string> protocols, bool verify)
    {
        error = {};
        using Result = std::pair<HttpResponse, Error>;
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        auto completed = [promise](HttpResponse response, const Error &failure) {
            promise->set_value(Result{std::move(response), failure});
        };
        const bool accepted = context
            ? client->requestTls(std::move(host), port, context, std::move(request),
                                 std::move(completed), std::move(protocols), verify)
            : client->request(std::move(host), port, std::move(request), std::move(completed));
        if (!accepted) { error = {UV_ECANCELED, "HTTP request could not be queued"}; return {}; }
        auto result = future.get();
        error = std::move(result.second);
        return std::move(result.first);
    }
};

HttpSyncClientSession::HttpSyncClientSession() : HttpSyncClientSession(HttpClient::Options{}) {}
HttpSyncClientSession::HttpSyncClientSession(HttpClient::Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
HttpSyncClientSession::~HttpSyncClientSession() = default;

HttpResponse HttpSyncClientSession::request(std::string host, std::uint16_t port,
                                           HttpRequest request, Error &error)
{
    return impl_->perform(std::move(host), port, nullptr, std::move(request), error, {}, true);
}

HttpResponse HttpSyncClientSession::requestTls(std::string host, std::uint16_t port, SSL_CTX *context,
                                              HttpRequest request, Error &error,
                                              std::vector<std::string> protocols, bool verify)
{
    if (!context) { error = {UV_EINVAL, "TLS context is required"}; return {}; }
    return impl_->perform(std::move(host), port, context, std::move(request), error,
                          std::move(protocols), verify);
}

ConnectionPoolStats HttpSyncClientSession::poolStats() const { return impl_->client->poolStats(); }

}  // namespace uvhttp
