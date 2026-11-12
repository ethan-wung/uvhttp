/**
 * @file        http2_client.h
 * @brief       HTTP/2 明文和 TLS 的池化、多路复用及流式客户端
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP2_CLIENT_H
#define UVHTTP_HTTP2_CLIENT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <openssl/ssl.h>
#include "uvhttp/export.h"
#include "uvhttp/http.h"
#include "uvhttp/connection_pool.h"
#include "uvhttp/tcp_client.h"
#include "uvhttp/http2_session.h"

namespace uvhttp
{
namespace detail
{
class ClientPool;
}
/**
 * @brief       HTTP/2 明文和 TLS 的池化、多路复用及流式客户端。
 */
class UVHTTP_EXPORT Http2Client
{
  public:
    /**
     * @brief       初始化配置；不与对象运行状态同步修改。
     */
    struct Options
    {
        /**
         * @brief       底层 TCP 配置，构造时读取。
         */
        TcpConnection::Options tcp;
        /**
         * @brief       HTTP/2 缓冲响应 body 上限（字节）。
         */
        std::size_t maxResponseBodyBytes{64 * 1024 * 1024};
        /**
         * @brief       请求或响应头字段大小上限（字节）。
         */
        std::size_t maxHeaderBytes{64 * 1024};
        /**
         * @brief       每个 origin 的连接数量上限，必须大于零。
         */
        std::size_t maxConnectionsPerOrigin{2};
        /**
         * @brief       本地单条 HTTP/2 连接并发流上限，还受远端 SETTINGS 限制。
         */
        std::size_t maxConcurrentStreamsPerConnection{100};
        /**
         * @brief       兼容的单 origin 排队上限，与 pool 对应上限取较小值。
         */
        std::size_t maxPendingRequests{1024};
        /**
         * @brief       共享连接池容量、空闲回收和超时配置。
         */
        ConnectionPoolOptions pool;
    };
    /**
     * @brief       完整请求完成回调 (HttpResponse, const Error&)；在 loop 执行，先检查错误。
     */
    using CompletionCallback = std::function<void(HttpResponse, const Error &)>;
    /**
     * @brief       流式响应的头、body、成功和失败回调。
     */
    struct StreamCallbacks
    {
        /**
         * @brief       收到最终响应头；不把中间 1xx 当作最终响应。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpResponseHead &)> onResponseHead;
        /**
         * @brief       收到响应 body 分块；数据视图仅在回调期间有效，需要保存时复制。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::string_view)> onResponseBody;
        /**
         * @brief       流式请求成功完成，不再产生响应 body。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void()> onComplete;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
    };
    /**
     * @brief       流式上传就绪及流式响应回调。
     */
    struct RequestStreamCallbacks
    {
        /**
         * @brief       HTTP/2 上传流已分配；参数为 session 和 streamId，使用 writeRequestData/endRequestData 上传。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Http2Session::Ptr &, std::int32_t)> onReady;
        /**
         * @brief       收到最终响应头；不把中间 1xx 当作最终响应。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpResponseHead &)> onResponseHead;
        /**
         * @brief       收到响应 body 分块；数据视图仅在回调期间有效，需要保存时复制。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::string_view)> onResponseBody;
        /**
         * @brief       流式请求成功完成，不再产生响应 body。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void()> onComplete;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
    };

    /**
     * @brief       构造 Http2Client 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    Http2Client(EventLoop &loop);
    /**
     * @brief       构造 Http2Client 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    Http2Client(EventLoop &loop, Options options);
    /**
     * @brief       释放 Http2Client 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    ~Http2Client();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     * @details     非法的连接池上限或超时配置可抛 std::invalid_argument；默认构造使用默认限额。
     */
    Http2Client(const Http2Client &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    Http2Client &operator=(const Http2Client &other) = delete;
    /**
     * @brief       获取线程安全的连接池统计快照
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @return      统计快照；各计数存在异步更新，不是跨字段事务快照。
     */
    ConnectionPoolStats poolStats() const;
    /**
     * @brief       请求关闭连接或客户端
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @return      无。
     * @details     永久关闭整个客户端池，不是仅清空空闲连接；后续提交将被拒绝。
     */
    void close() noexcept;
    /**
     * @brief       通过 TLS 提交缓冲上传、流式响应的 HTTP/2 请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。
     */
    bool requestStreamTls(std::string host, std::uint16_t port, SSL_CTX *context,
                          HttpRequest request, StreamCallbacks callbacks, bool verifyPeer = true);
    /**
     * @brief       提交 TLS HTTP/2 流式上传和下载请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。
     */
    bool requestStreamBodyTls(std::string host, std::uint16_t port, SSL_CTX *context,
                              HttpRequestHead request, RequestStreamCallbacks callbacks,
                              bool verifyPeer = true);
    /**
     * @brief       通过 TLS 提交完整 HTTP 请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] completed 最终完成回调，在所属 loop 调用；先检查 Error，再使用响应
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。
     */
    bool requestTls(std::string host, std::uint16_t port, SSL_CTX *context, HttpRequest request,
                    CompletionCallback completed, bool verifyPeer = true);
    /**
     * @brief       提交明文 prior-knowledge HTTP/2 请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] completed 最终完成回调，在所属 loop 调用；先检查 Error，再使用响应
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。 直接使用 HTTP/2 prior-knowledge，不执行 HTTP/1.1 h2c Upgrade。
     */
    bool requestPlaintext(std::string host, std::uint16_t port, HttpRequest request,
                          CompletionCallback completed);
    /**
     * @brief       提交缓冲上传、流式下载的明文 HTTP/2 请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。 直接使用 HTTP/2 prior-knowledge，不执行 HTTP/1.1 h2c Upgrade。
     */
    bool requestStreamPlaintext(std::string host, std::uint16_t port, HttpRequest request,
                                StreamCallbacks callbacks);
    /**
     * @brief       提交明文 HTTP/2 流式上传和下载请求
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     全部异步请求尽量复用按 origin 隔离的连接池。true 仅表示接受提交，最终结果由 loop 回调交付；立即返回 false 时不能等待完成回调。 直接使用 HTTP/2 prior-knowledge，不执行 HTTP/1.1 h2c Upgrade。
     */
    bool requestStreamBodyPlaintext(std::string host, std::uint16_t port, HttpRequestHead request,
                                    RequestStreamCallbacks callbacks);

    /** @brief 通过 HTTP/1.1 h2c Upgrade 提交明文缓冲请求；升级连接在独立 origin 池内复用，不降级到 HTTP/1。 */
    bool requestUpgrade(std::string host, std::uint16_t port, HttpRequest request, CompletionCallback completed);
    /** @brief h2c 升级请求使用缓冲上传、流式响应；连接复用策略同 requestUpgrade。 */
    bool requestStreamUpgrade(std::string host, std::uint16_t port, HttpRequest request, StreamCallbacks callbacks);
  private:
    EventLoop &loop_;
    Options options_;
    TcpClient tcpClient_;
    std::shared_ptr<detail::ClientPool> pool_;
};
} // namespace uvhttp
#endif
