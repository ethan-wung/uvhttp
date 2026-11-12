/**
 * @file        http_client.cc
 * @brief       按 origin 池化的 HTTP/1.x 异步明文与 TLS 客户端的实现。
 * @author      Ethan
 * @version     1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http_client.h"
#include "client_pool.h"
#include <algorithm>
#include <utility>
namespace uvhttp {
HttpClient::HttpClient(EventLoop &loop) : HttpClient(loop, Options{}) {}
HttpClient::HttpClient(EventLoop &loop, Options options) : loop_(loop), options_(std::move(options)), tcpClient_(loop, options_.tcp) {
    options_.pool.maxPendingRequestsPerOrigin = std::min(options_.pool.maxPendingRequestsPerOrigin, options_.maxPersistentRequests);
    persistent_ = std::make_shared<detail::ClientPool>(loop, options_.tcp, options_.pool, false, 1, options_.maxBufferedResponseBodyBytes, options_.maxHeaderBytes);
}
HttpClient::~HttpClient() { closePersistent(); }
ConnectionPoolStats HttpClient::poolStats() const { return persistent_->stats(); }
void HttpClient::closePersistent() noexcept { persistent_->close(); }
bool HttpClient::requestPersistent(std::string host, std::uint16_t port, HttpRequest request, CompletionCallback completed) {
    return this->request(std::move(host), port, std::move(request), std::move(completed));
}
bool HttpClient::request(std::string host, std::uint16_t port, HttpRequest request, CompletionCallback completed) {
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request); job->completed = std::move(completed);
    return persistent_->submit(std::move(host), port, nullptr, true, {}, std::move(job));
}
bool HttpClient::requestStream(std::string host, std::uint16_t port, HttpRequestHead request, UploadReadyCallback upload, StreamCallbacks callbacks) {
    auto job = std::make_shared<detail::ClientPool::Job>();
    static_cast<HttpRequestHead &>(job->request) = std::move(request);
    job->streaming = true; job->upload = true; job->uploadReady = std::move(upload); job->callbacks = std::move(callbacks);
    return persistent_->submit(std::move(host), port, nullptr, true, {}, std::move(job));
}
bool HttpClient::requestTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequest request, CompletionCallback completed, std::vector<std::string> alpn, bool verify) {
    if (!context) return false;
    auto job = std::make_shared<detail::ClientPool::Job>();
    job->request = std::move(request); job->completed = std::move(completed);
    return persistent_->submit(std::move(host), port, context, verify, std::move(alpn), std::move(job));
}
bool HttpClient::requestStreamTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequestHead request, UploadReadyCallback upload, StreamCallbacks callbacks, std::vector<std::string> alpn, bool verify) {
    if (!context) return false;
    auto job = std::make_shared<detail::ClientPool::Job>();
    static_cast<HttpRequestHead &>(job->request) = std::move(request);
    job->streaming = true; job->upload = true; job->uploadReady = std::move(upload); job->callbacks = std::move(callbacks);
    return persistent_->submit(std::move(host), port, context, verify, std::move(alpn), std::move(job));
}
}
