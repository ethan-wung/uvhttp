/**
 * @file        http_sync_client.h
 * @brief       HTTP/HTTPS 阻塞客户端及可复用连接池的同步会话
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP_SYNC_CLIENT_H
#define UVHTTP_HTTP_SYNC_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>
#include <openssl/ssl.h>

#include "uvhttp/export.h"
#include "uvhttp/http.h"
#include "uvhttp/http_client.h"

namespace uvhttp
{

/**
 * @brief       一次性阻塞 HTTP/HTTPS 客户端，不跨静态调用复用连接。
 */
class UVHTTP_EXPORT HttpSyncClient
{
  public:
    /**
     * @brief       提交完整 HTTP 请求
     * @threadsafe  是；不得与析构并发，每次调用的 Error 输出对象须独占。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[out] error 输出调用结果，code=0 表示成功，非零时不要把响应当作成功结果
     * @return      响应值；同步请求须先检查输出 Error；握手辅助函数仅生成报文。
     * @details     调用线程阻塞到完成/失败；不得阻塞请求处理所依赖的事件循环。静态客户端不跨调用复用，Session 在对象生命周期内复用。
     */
    static HttpResponse request(std::string host, std::uint16_t port, HttpRequest request,
                                Error &error);
    /**
     * @brief       通过 TLS 提交完整 HTTP 请求
     * @threadsafe  是；不得与析构并发，每次调用的 Error 输出对象须独占。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[out] error 输出调用结果，code=0 表示成功，非零时不要把响应当作成功结果
     * @param[in] alpnProtocols 期望协商的 ALPN 协议列表，按调用方提供的顺序传入
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      响应值；同步请求须先检查输出 Error；握手辅助函数仅生成报文。
     * @details     调用线程阻塞到完成/失败；不得阻塞请求处理所依赖的事件循环。静态客户端不跨调用复用，Session 在对象生命周期内复用。
     */
    static HttpResponse requestTls(std::string host, std::uint16_t port, SSL_CTX *context,
                                   HttpRequest request, Error &error,
                                   std::vector<std::string> alpnProtocols = {"http/1.1"},
                                   bool verifyPeer = true);
};

/**
 * @brief       持有专用 loop 线程和连接池的阻塞 HTTP/HTTPS 客户端。
 */
class UVHTTP_EXPORT HttpSyncClientSession
{
  public:
    /**
     * @brief       构造 HttpSyncClientSession 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    HttpSyncClientSession();
    /**
     * @brief       构造 HttpSyncClientSession 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    explicit HttpSyncClientSession(HttpClient::Options options);
    /**
     * @brief       释放 HttpSyncClientSession 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     关闭内部客户端池、停止内部事件循环并 join 工作线程；不能与阻塞请求并发析构。
     */
    ~HttpSyncClientSession();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    HttpSyncClientSession(const HttpSyncClientSession &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    HttpSyncClientSession &operator=(const HttpSyncClientSession &other) = delete;
    /**
     * @brief       提交完整 HTTP 请求
     * @threadsafe  是；不得与析构并发，每次调用的 Error 输出对象须独占。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[out] error 输出调用结果，code=0 表示成功，非零时不要把响应当作成功结果
     * @return      响应值；同步请求须先检查输出 Error；握手辅助函数仅生成报文。
     * @details     调用线程阻塞到完成/失败；不得阻塞请求处理所依赖的事件循环。静态客户端不跨调用复用，Session 在对象生命周期内复用。
     */
    HttpResponse request(std::string host, std::uint16_t port, HttpRequest request, Error &error);
    /**
     * @brief       通过 TLS 提交完整 HTTP 请求
     * @threadsafe  是；不得与析构并发，每次调用的 Error 输出对象须独占。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[out] error 输出调用结果，code=0 表示成功，非零时不要把响应当作成功结果
     * @param[in] alpnProtocols 期望协商的 ALPN 协议列表，按调用方提供的顺序传入
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      响应值；同步请求须先检查输出 Error；握手辅助函数仅生成报文。
     * @details     调用线程阻塞到完成/失败；不得阻塞请求处理所依赖的事件循环。静态客户端不跨调用复用，Session 在对象生命周期内复用。
     */
    HttpResponse requestTls(std::string host, std::uint16_t port, SSL_CTX *context,
                            HttpRequest request, Error &error,
                            std::vector<std::string> alpnProtocols = {"http/1.1"},
                            bool verifyPeer = true);
    /**
     * @brief       获取线程安全的连接池统计快照
     * @threadsafe  是；不得与析构并发，每次调用的 Error 输出对象须独占。
     * @return      统计快照；各计数存在异步更新，不是跨字段事务快照。
     */
    ConnectionPoolStats poolStats() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uvhttp

#endif
