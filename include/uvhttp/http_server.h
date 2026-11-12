/**
 * @file        http_server.h
 * @brief       HTTP/1.x 异步服务端、流式请求、TLS 和 WebSocket 升级
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP_SERVER_H
#define UVHTTP_HTTP_SERVER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "uvhttp/export.h"
#include "uvhttp/http.h"
#include "uvhttp/websocket.h"
#include "uvhttp/tcp_server.h"
#include <openssl/ssl.h>
#include <vector>
#include <chrono>
#ifdef UVHTTP_WITH_HTTP2
#include "uvhttp/http2_session.h"
#endif

namespace uvhttp
{

/**
 * @brief       HTTP/1.x 异步服务端、流式请求、TLS 和 WebSocket 升级。
 */
class UVHTTP_EXPORT HttpServer
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
        TcpServer::Options tcp;
        /**
         * @brief       缓冲请求 body 上限（字节）；流式接收须由业务限制总量。
         */
        std::size_t maxBufferedRequestBodyBytes{16 * 1024 * 1024};
        /**
         * @brief       请求或响应头字段大小上限（字节）。
         */
        std::size_t maxHeaderBytes{64 * 1024};
        /**
         * @brief       服务端 OpenSSL 上下文；调用者持有至服务端停止，不得并发修改。
         */
        SSL_CTX *tlsContext{nullptr};
        /**
         * @brief       服务端可协商的 ALPN 协议列表，HTTP/1 默认 http/1.1。
         */
        std::vector<std::string> tlsAlpnProtocols{"http/1.1"};
        /**
         * @brief       升级后的 WebSocket 帧负载上限（字节）。
         */
        std::size_t maxWebSocketPayloadBytes{64 * 1024 * 1024};
#ifdef UVHTTP_WITH_HTTP2
        /** @brief 启用统一 HTTP/1 与 HTTP/2 入口；TLS 使用 ALPN，明文使用连接前言识别。 */
        bool enableHttp2{false};
        /** @brief 接受明文 HTTP/1.1 h2c Upgrade；须同时启用 enableHttp2。 */
        bool enableH2cUpgrade{false};
        /** @brief HTTP/2 每连接并发流上限。 */
        std::uint32_t maxHttp2ConcurrentStreams{100};
#endif
        /** @brief 明文协议识别最长等待时间，必须大于零。 */
        std::chrono::milliseconds protocolDetectionTimeout{10000};

    };

    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       监听成功通知；参数为实际绑定端口，动态端口应在此读取。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::uint16_t)> onListening;
        /**
         * @brief       收到完整缓冲请求；参数依次为 const HttpRequest& 和 HttpResponseWriter。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpRequest &, HttpResponseWriter)> onRequest;
        /**
         * @brief       收到请求头；参数依次为请求头和 writer。返回 true 选择流式 body/end 回调，false 选择缓冲 onRequest。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<bool(const HttpRequestHead &, HttpResponseWriter)> onRequestHead;
        /**
         * @brief       收到请求 body 分块；参数依次为请求头和 body 视图，两者只在回调期间有效。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpRequestHead &, std::string_view)> onRequestBody;
        /**
         * @brief       请求 body 接收结束；参数依次为请求头和响应 writer。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpRequestHead &, HttpResponseWriter)> onRequestEnd;
        /**
         * @brief       HTTP 升级成功；参数为升级请求头与 WebSocket 连接。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const HttpRequestHead &, WebSocketConnection::Ptr)> onWebSocket;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
        /**
         * @brief       监听器及其连接停止完成；可在此结束应用事件循环。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void()> onStopped;
#ifdef UVHTTP_WITH_HTTP2
        /** @brief 统一 listener 的 HTTP/2 回调；请求使用 session/streamId 回复，均在所属 loop 执行。 */
        Http2Session::Callbacks http2;
#endif

    };

    /**
     * @brief       构造 HttpServer 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] address 监听地址，通常为本地 IPv4/IPv6 字面地址
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     */
    HttpServer(EventLoop &loop, std::string address, std::uint16_t port, Callbacks callbacks);
    /**
     * @brief       构造 HttpServer 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] address 监听地址，通常为本地 IPv4/IPv6 字面地址
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     */
    HttpServer(EventLoop &loop, std::string address, std::uint16_t port, Options options,
               Callbacks callbacks);
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    HttpServer(const HttpServer &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    HttpServer &operator=(const HttpServer &other) = delete;
    /**
     * @brief       释放 HttpServer 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     先 stop 并等待 onStopped，再释放最后的所有者；析构不是阻塞式等待关闭。
     */
    ~HttpServer();

    /**
     * @brief       启动服务或事件循环线程
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool start() noexcept;
    /**
     * @brief       异步请求停止
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool stop() noexcept;
    /**
     * @brief       获取实际监听端口
     * @threadsafe  否；在所属 loop 读取，或在监听完成并建立外部同步后读取。
     * @return      端口号；尚未绑定或查询失败时为 0。
     */
    std::uint16_t port() const noexcept;
    /**
     * @brief       查询当前状态
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      当前生命周期状态快照，可能在返回后变化。
     */
    TcpServer::State state() const noexcept;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace uvhttp

#endif


