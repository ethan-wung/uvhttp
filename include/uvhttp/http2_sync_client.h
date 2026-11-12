/**
 * @file http2_sync_client.h
 * @brief HTTP/2 阻塞客户端，内部使用独立 libuv 线程和连接池。
 * @author Ethan
 * @version 1.0 初次建立
 * @date 2026.11.11
 */
#ifndef UVHTTP_HTTP2_SYNC_CLIENT_H
#define UVHTTP_HTTP2_SYNC_CLIENT_H
#include "uvhttp/http2_client.h"
namespace uvhttp {
/** @brief 可复用 HTTP/2 同步会话；调用会阻塞，不得阻塞所依赖的服务器 loop。 */
class UVHTTP_EXPORT Http2SyncClientSession {
public:
    /** @brief 使用默认连接池配置构造。 */
    Http2SyncClientSession();
    /** @brief 使用指定连接池配置构造，非法配置抛出异常。 */
    explicit Http2SyncClientSession(Http2Client::Options options);
    /** @brief 关闭连接并等待线程退出；不得与请求并发析构。 */
    ~Http2SyncClientSession();
    Http2SyncClientSession(const Http2SyncClientSession &) = delete;
    Http2SyncClientSession &operator=(const Http2SyncClientSession &) = delete;
    /** @brief 同步明文 prior-knowledge 请求；先检查输出 error。允许多个调用线程并发请求。 */
    HttpResponse requestPlaintext(std::string host, std::uint16_t port, HttpRequest request, Error &error);
    /** @brief 同步明文 h2c Upgrade 请求；成功升级的连接在独立池键下复用，不进行协议降级。 */
    HttpResponse requestUpgrade(std::string host, std::uint16_t port, HttpRequest request, Error &error);
    /** @brief 同步 TLS/ALPN h2 请求；context 非空且不得并发修改，默认校验证书和主机名。 */
    HttpResponse requestTls(std::string host, std::uint16_t port, SSL_CTX *context,
                            HttpRequest request, Error &error, bool verifyPeer = true);
    /** @brief 取得线程安全的连接池统计快照。 */
    ConnectionPoolStats poolStats() const;
    /** @brief 取消当前会话的全部请求并关闭连接；此会话之后不再接受请求。 */
    void close() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
/** @brief 一次性阻塞 HTTP/2 客户端；需要跨请求复用时使用 Http2SyncClientSession。 */
class UVHTTP_EXPORT Http2SyncClient {
public:
    /** @brief 同步明文请求，返回前关闭本次连接池；error 为输出错误。 */
    static HttpResponse requestPlaintext(std::string host, std::uint16_t port, HttpRequest request, Error &error);
    /** @brief 一次性同步 h2c Upgrade 请求。 */
    static HttpResponse requestUpgrade(std::string host, std::uint16_t port, HttpRequest request, Error &error);
    /** @brief 同步 TLS 请求，默认验证证书；error 为输出错误。 */
    static HttpResponse requestTls(std::string host, std::uint16_t port, SSL_CTX *context,
                                   HttpRequest request, Error &error, bool verifyPeer = true);
};
}
#endif
