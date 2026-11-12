/**
 * @file        http2_session.h
 * @brief       基于 nghttp2 的 HTTP/2 会话、流和协议事件接口
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP2_SESSION_H
#define UVHTTP_HTTP2_SESSION_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "uvhttp/async_byte_stream.h"
#include "uvhttp/http.h"

namespace uvhttp
{

/**
 * @brief       基于 nghttp2 的 HTTP/2 会话、流和协议事件接口。
 */
class UVHTTP_EXPORT Http2Session : public std::enable_shared_from_this<Http2Session>
{
  public:
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<Http2Session>;
    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       收到完整缓冲请求；参数依次为 session、连接内 streamId 和按值交付的 HttpRequest。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, std::int32_t, HttpRequest)> onRequest;
        /**
         * @brief       收到请求头；参数依次为 session、streamId 和按值交付的请求头。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, std::int32_t, HttpRequestHead)> onRequestHead;
        /**
         * @brief       收到请求 body 分块；string_view 仅在回调期间有效。HTTP/2 streamId 仅在当前连接内唯一。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t, std::string_view)> onRequestBody;
        /**
         * @brief       请求 body 接收结束；参数依次为 session 和 streamId。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, std::int32_t)> onRequestEnd;
        /**
         * @brief       收到完整缓冲响应；参数为连接内 streamId 和响应。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t, HttpResponse)> onResponse;
        /**
         * @brief       收到最终响应头；不把中间 1xx 当作最终响应。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t, HttpResponseHead)> onResponseHead;
        /**
         * @brief       收到响应 body 分块；数据视图仅在回调期间有效，需要保存时复制。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t, std::string_view)> onResponseBody;
        /**
         * @brief       指定 HTTP/2 流的最终响应接收结束。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t)> onResponseEnd;
        /**
         * @brief       收到 GOAWAY；参数为对端最后接纳的流编号。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t)> onGoAway;
        /**
         * @brief       远端 SETTINGS 更新并发流上限；参数为新上限。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::size_t)> onRemoteMaxConcurrentStreams;
        /**
         * @brief       已处理远端非 ACK SETTINGS，可据此调整调度。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void()> onRemoteSettings;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
        /**
         * @brief       HTTP/2 流关闭；参数为 streamId 和协议错误码。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(std::int32_t, std::uint32_t)> onStreamClosed;
        /** @brief 带会话身份的请求分块回调，跨连接用(session,streamId)标识；设置后优先于旧 onRequestBody。 */
        std::function<void(const Ptr &, std::int32_t, std::string_view)> onRequestBodyWithSession;
    };

    /**
     * @brief       创建 HTTP/2 客户端协议会话
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] stream 底层字节流；协议会话持有其共享所有权
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] maxHeaderBytes 头字段累计字节限制
     * @param[in] maxBodyBytes 缓冲 body 字节上限；流式 body 由应用另行限制总量
     * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
     */
    static Ptr createClient(AsyncByteStream::Ptr stream, Callbacks callbacks,
                            std::size_t maxHeaderBytes = 64 * 1024,
                            std::size_t maxBodyBytes = 64 * 1024 * 1024);
    /**
     * @brief       创建 HTTP/2 服务端协议会话
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] stream 底层字节流；协议会话持有其共享所有权
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] maxConcurrentStreams 服务端允许的并发 HTTP/2 流数量
     * @param[in] maxBodyBytes 缓冲 body 字节上限；流式 body 由应用另行限制总量
     * @param[in] maxHeaderBytes 头字段累计字节限制
     * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
     */
    static Ptr createServer(AsyncByteStream::Ptr stream, Callbacks callbacks,
                            std::uint32_t maxConcurrentStreams = 100,
                            std::size_t maxBodyBytes = 64 * 1024 * 1024,
                            std::size_t maxHeaderBytes = 64 * 1024);
    /**
     * @brief       释放 Http2Session 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    ~Http2Session();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    Http2Session(const Http2Session &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    Http2Session &operator=(const Http2Session &other) = delete;

    /**
     * @brief       向 HTTP/2 会话输入解密后的增量字节
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] bytes 本次输入字节；string_view 只在调用期间有效，底层按接口语义消费/复制
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool receive(std::string_view bytes) noexcept;
    /**
     * @brief       提交完整 HTTP 请求
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @return      成功返回当前连接内的流编号，失败返回负值。
     */
    std::int32_t request(HttpRequest request) noexcept;
    /**
     * @brief       分配 HTTP/2 流并提交流式请求头
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @return      成功返回当前连接内的流编号，失败返回负值。
     */
    std::int32_t requestStream(HttpRequestHead request) noexcept;
    /**
     * @brief       提交完整 HTTP/2 响应或中间响应
     * @threadsafe  是；跨线程调用投递至所属 loop，不保证任务执行时该流仍存在。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @param[in] response 响应状态、头字段及可选缓冲 body
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     跨线程返回 true 只代表成功投递；执行时流可能已关闭。respond 可先发非101的 1xx；respondHead 仅用于最终响应。
     */
    bool respond(std::int32_t streamId, HttpResponse response) noexcept;
    /**
     * @brief       提交 HTTP/2 最终响应头并开始流式响应
     * @threadsafe  是；跨线程调用投递至所属 loop，不保证任务执行时该流仍存在。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @param[in] response 响应状态、头字段及可选缓冲 body
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     跨线程返回 true 只代表成功投递；执行时流可能已关闭。respond 可先发非101的 1xx；respondHead 仅用于最终响应。
     */
    bool respondHead(std::int32_t streamId, HttpResponseHead response) noexcept;
    /**
     * @brief       提交 HTTP/2 响应 body 分块
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @param[in] chunk 本次 body 数据块；不代表 TCP 包或对端回调的边界
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     待发数据队列尚无完整公开高水位通知；应用须自行限速、限制总缓存，不要无限生产数据。
     */
    bool writeData(std::int32_t streamId, std::string chunk) noexcept;
    /**
     * @brief       提交 HTTP/2 请求 body 分块
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @param[in] chunk 本次 body 数据块；不代表 TCP 包或对端回调的边界
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     待发数据队列尚无完整公开高水位通知；应用须自行限速、限制总缓存，不要无限生产数据。
     */
    bool writeRequestData(std::int32_t streamId, std::string chunk) noexcept;
    /**
     * @brief       结束 HTTP/2 响应 body
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool endData(std::int32_t streamId) noexcept;
    /**
     * @brief       结束 HTTP/2 请求 body
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool endRequestData(std::int32_t streamId) noexcept;
    /**
     * @brief       发送 GOAWAY 并声明最后处理的流编号
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] lastStreamId GOAWAY 中声明的最后处理流编号，必须非负
     * @param[in] errorCode HTTP/2 协议错误码；RST 默认 8（CANCEL），GOAWAY 默认 0
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool goAway(std::int32_t lastStreamId, std::uint32_t errorCode = 0) noexcept;
    /**
     * @brief       发送 RST_STREAM 终止指定流
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @param[in] streamId 当前 HTTP/2 连接内的流编号，不是跨连接唯一标识
     * @param[in] errorCode HTTP/2 协议错误码；RST 默认 8（CANCEL），GOAWAY 默认 0
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     实现包含跨线程投递分支，但关闭状态读取未全面同步；安全用法仍是先 post 至所属 loop。
     */
    bool resetStream(std::int32_t streamId, std::uint32_t errorCode = 8) noexcept;
    /**
     * @brief       查询是否为客户端会话
     * @threadsafe  否；在所属 loop 串行调用，不能与会话关闭并发。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isClient() const noexcept;

    /** @brief 将已完整接收的 h2c 升级请求导入流1；在 loop 调用，仅服务端新会话可用。 */
    bool upgradeServer(std::string_view settings, HttpRequest request) noexcept;
    /** @brief 在客户端收到101后创建升级会话；settings 为已发送头的解码字节，响应从流1交付。 */
    static Ptr createUpgradeClient(AsyncByteStream::Ptr stream, Callbacks callbacks,
                                    std::string_view settings, bool headRequest,
                                    std::size_t maxHeaderBytes = 65536,
                                    std::size_t maxBodyBytes = 16 * 1024 * 1024);
  private:
    struct Impl;
    explicit Http2Session(std::unique_ptr<Impl> impl);
    static Ptr create(AsyncByteStream::Ptr stream, Callbacks callbacks, bool client,
                      std::uint32_t maxConcurrentStreams, std::size_t maxBodyBytes,
                      std::size_t maxHeaderBytes, bool deferOutput = false);
    void flushOutput() noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace uvhttp

#endif






