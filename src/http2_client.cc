/**
 * @file        http2_client.cc
 * @brief       HTTP/2 明文和 TLS 的池化、多路复用及流式客户端的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http2_client.h"
#include "client_pool.h"
#include <algorithm>
#include <utility>
namespace uvhttp {
Http2Client::Http2Client(EventLoop &loop) : Http2Client(loop, Options{}) {}
Http2Client::Http2Client(EventLoop &loop, Options options) : loop_(loop), options_(std::move(options)), tcpClient_(loop, options_.tcp) {
    options_.pool.maxConnectionsPerOrigin = std::min(options_.pool.maxConnectionsPerOrigin, options_.maxConnectionsPerOrigin);
    options_.pool.maxPendingRequestsPerOrigin = std::min(options_.pool.maxPendingRequestsPerOrigin, options_.maxPendingRequests);
    pool_ = std::make_shared<detail::ClientPool>(loop, options_.tcp, options_.pool, true, options_.maxConcurrentStreamsPerConnection, options_.maxResponseBodyBytes, options_.maxHeaderBytes);
}
Http2Client::~Http2Client() { close(); }
bool Http2Client::requestUpgrade(std::string host,std::uint16_t port,HttpRequest request,CompletionCallback completed) {
    auto job=std::make_shared<detail::ClientPool::Job>();
    job->request=std::move(request); job->completed=std::move(completed); job->h2cUpgrade=true;
    return pool_->submit(std::move(host),port,nullptr,true,{},std::move(job));
}
bool Http2Client::requestStreamUpgrade(std::string host,std::uint16_t port,HttpRequest request,StreamCallbacks callbacks) {
    auto job=std::make_shared<detail::ClientPool::Job>();
    job->request=std::move(request); job->h2cUpgrade=true; job->streaming=true;
    job->callbacks={std::move(callbacks.onResponseHead),std::move(callbacks.onResponseBody),std::move(callbacks.onComplete),std::move(callbacks.onError)};
    return pool_->submit(std::move(host),port,nullptr,true,{},std::move(job));
}
void Http2Client::close() noexcept { pool_->close(); }
ConnectionPoolStats Http2Client::poolStats() const { return pool_->stats(); }
bool Http2Client::requestPlaintext(std::string host, std::uint16_t port, HttpRequest request, CompletionCallback completed) {
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request);
    job->completed = std::move(completed);
    return pool_->submit(std::move(host), port, nullptr, true, {}, std::move(job));
}
bool Http2Client::requestStreamPlaintext(std::string host, std::uint16_t port, HttpRequest request, StreamCallbacks callbacks) {
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request);
    job->streaming = true;
    job->callbacks = {std::move(callbacks.onResponseHead), std::move(callbacks.onResponseBody), std::move(callbacks.onComplete), std::move(callbacks.onError)};
    return pool_->submit(std::move(host), port, nullptr, true, {}, std::move(job));
}
bool Http2Client::requestStreamBodyPlaintext(std::string host, std::uint16_t port, HttpRequestHead request, RequestStreamCallbacks callbacks) {
    auto job = std::make_shared<detail::ClientPool::Job>();
    static_cast<HttpRequestHead &>(job->request) = std::move(request);
    job->streaming = true;
    job->callbacks = {std::move(callbacks.onResponseHead), std::move(callbacks.onResponseBody), std::move(callbacks.onComplete), std::move(callbacks.onError)};
    job->upload = true; job->h2Ready = std::move(callbacks.onReady);
    return pool_->submit(std::move(host), port, nullptr, true, {}, std::move(job));
}
bool Http2Client::requestTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequest request, CompletionCallback completed, bool verify) {
    if (!context) return false;
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request);
    job->completed = std::move(completed);
    return pool_->submit(std::move(host), port, context, verify, {"h2"}, std::move(job));
}
bool Http2Client::requestStreamTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequest request, StreamCallbacks callbacks, bool verify) {
    if (!context) return false;
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request);
    job->streaming = true;
    job->callbacks = {std::move(callbacks.onResponseHead), std::move(callbacks.onResponseBody), std::move(callbacks.onComplete), std::move(callbacks.onError)};
    return pool_->submit(std::move(host), port, context, verify, {"h2"}, std::move(job));
}
bool Http2Client::requestStreamBodyTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequestHead request, RequestStreamCallbacks callbacks, bool verify) {
    if (!context) return false;
    auto job = std::make_shared<detail::ClientPool::Job>();
    static_cast<HttpRequestHead &>(job->request) = std::move(request);
    job->streaming = true;
    job->callbacks = {std::move(callbacks.onResponseHead), std::move(callbacks.onResponseBody), std::move(callbacks.onComplete), std::move(callbacks.onError)};
    job->upload = true; job->h2Ready = std::move(callbacks.onReady);
    return pool_->submit(std::move(host), port, context, verify, {"h2"}, std::move(job));
}
}
