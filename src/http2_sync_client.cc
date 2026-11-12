/**
 * @file http2_sync_client.cc
 * @brief HTTP/2 连接池同步外观实现。
 * @author Ethan
 * @version 1.0 初次建立
 * @date 2026.11.11
 */
#include "uvhttp/http2_sync_client.h"
#include "uvhttp/event_loop_thread.h"
#include <future>
namespace uvhttp {
struct Http2SyncClientSession::Impl {
    EventLoopThread thread;
    std::unique_ptr<Http2Client> client;
    explicit Impl(Http2Client::Options options) {
        client = std::make_unique<Http2Client>(thread.start(), std::move(options));
    }
    ~Impl() { client.reset(); thread.stop(); thread.join(); }
    HttpResponse perform(std::string host, std::uint16_t port, SSL_CTX *context,
                         HttpRequest request, Error &error, bool verify, bool upgrade=false) {
        using Result = std::pair<HttpResponse, Error>;
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        auto complete = [promise](HttpResponse response, const Error &failure) {
            promise->set_value(Result{std::move(response), failure});
        };
        const bool accepted = context
            ? client->requestTls(std::move(host), port, context, std::move(request), complete, verify)
            : upgrade ? client->requestUpgrade(std::move(host), port, std::move(request), complete)
                      : client->requestPlaintext(std::move(host), port, std::move(request), complete);
        if (!accepted) { error={UV_ECANCELED,"HTTP/2 request submission rejected"}; return {}; }
        auto result=future.get(); error=std::move(result.second); return std::move(result.first);
    }
};
Http2SyncClientSession::Http2SyncClientSession() : Http2SyncClientSession(Http2Client::Options{}) {}
Http2SyncClientSession::Http2SyncClientSession(Http2Client::Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Http2SyncClientSession::~Http2SyncClientSession() = default;
HttpResponse Http2SyncClientSession::requestPlaintext(std::string host,std::uint16_t port,HttpRequest request,Error &error) {
    return impl_->perform(std::move(host),port,nullptr,std::move(request),error,true);
}
HttpResponse Http2SyncClientSession::requestTls(std::string host,std::uint16_t port,SSL_CTX *context,HttpRequest request,Error &error,bool verify) {
    if (!context) { error={UV_EINVAL,"TLS context is required"}; return {}; }
    return impl_->perform(std::move(host),port,context,std::move(request),error,verify);
}
HttpResponse Http2SyncClientSession::requestUpgrade(std::string host,std::uint16_t port,HttpRequest request,Error &error) {
    return impl_->perform(std::move(host),port,nullptr,std::move(request),error,true,true);
}
HttpResponse Http2SyncClient::requestUpgrade(std::string host,std::uint16_t port,HttpRequest request,Error &error) {
    Http2SyncClientSession client; return client.requestUpgrade(std::move(host),port,std::move(request),error);
}
ConnectionPoolStats Http2SyncClientSession::poolStats() const { return impl_->client->poolStats(); }
void Http2SyncClientSession::close() noexcept { impl_->client->close(); }
HttpResponse Http2SyncClient::requestPlaintext(std::string host,std::uint16_t port,HttpRequest request,Error &error) {
    Http2SyncClientSession client; return client.requestPlaintext(std::move(host),port,std::move(request),error);
}
HttpResponse Http2SyncClient::requestTls(std::string host,std::uint16_t port,SSL_CTX *context,HttpRequest request,Error &error,bool verify) {
    Http2SyncClientSession client; return client.requestTls(std::move(host),port,context,std::move(request),error,verify);
}
}
