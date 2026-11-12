/**
 * @file        tcp_connection.h
 * @brief       事件循环所属的 TCP 连接、流式收发及背压控制
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_TCP_CONNECTION_H
#define UVHTTP_TCP_CONNECTION_H

#include <atomic>

#include "uvhttp/export.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <uv.h>

#include "uvhttp/async_byte_stream.h"
#include "uvhttp/error.h"
#include "uvhttp/event_loop.h"

namespace uvhttp
{

class TcpServer;

/**
 * @brief       事件循环所属的 TCP 连接、流式收发及背压控制。
 */
class UVHTTP_EXPORT TcpConnection : public AsyncByteStream,
                                    public std::enable_shared_from_this<TcpConnection>
{
  public:
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<TcpConnection>;

    /**
     * @brief       对象生命周期状态。
     */
    enum class State : unsigned char
    {
        /**
         * @brief       正在解析地址或建立连接。
         */
        Connecting,
        /**
         * @brief       连接已建立。
         */
        Connected,
        /**
         * @brief       正在进行写半关闭或连接关闭。
         */
        Disconnecting,
        /**
         * @brief       连接/字节流已关闭。
         */
        Closed
    };

    /**
     * @brief       TCP 写入接受、背压或未连接状态。
     */
    enum class WriteResult : unsigned char
    {
        /**
         * @brief       写请求已排队，等待异步完成。
         */
        Queued,
        /**
         * @brief       暂时不能接受更多写入；TLS 失败也可能映射到此值。
         */
        Backpressured,
        /**
         * @brief       连接未建立或正在关闭，不能接受写入。
         */
        NotConnected
    };

    /**
     * @brief       初始化配置；不与对象运行状态同步修改。
     */
    struct Options
    {
        /**
         * @brief       TCP 待写缓冲上限（字节），超出时拒绝接受新写入。
         */
        std::size_t maxPendingWriteBytes{4 * 1024 * 1024};
        /**
         * @brief       TCP 待写量下降到该阈值时触发写排空通知（字节）。
         */
        std::size_t writeLowWatermarkBytes{1024 * 1024};
    };

    /**
     * @brief       连接成功回调 (const Ptr&)。
     */
    using ConnectedCallback = std::function<void(const Ptr &)>;
    /**
     * @brief       数据回调 (const Ptr&, string_view)，视图仅在调用期间有效。
     */
    using DataCallback = std::function<void(const Ptr &, std::string_view)>;
    /**
     * @brief       对端写半关闭回调 (const Ptr&)。
     */
    using PeerHalfCloseCallback = std::function<void(const Ptr &)>;
    /**
     * @brief       错误回调；TCP 使用连接和 Error，帧解析器使用错误字符串。
     */
    using ErrorCallback = std::function<void(const Ptr &, const Error &)>;
    /**
     * @brief       连接关闭回调 (const Ptr&)。
     */
    using ClosedCallback = std::function<void(const Ptr &)>;
    /**
     * @brief       写入完成回调 (const Ptr&, const Error&)，不是远端应用确认。
     */
    using WriteCallback = std::function<void(const Ptr &, const Error &)>;
    /**
     * @brief       建连完成回调 (const Ptr&, const Error&)。
     */
    using ConnectCallback = std::function<void(const Ptr &, const Error &)>;
    /**
     * @brief       写缓冲下降通知 (const Ptr&)。
     */
    using WriteDrainedCallback = std::function<void(const Ptr &)>;

    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       连接成功通知；参数为连接共享指针，可在此安装后续回调。
         */
        ConnectedCallback onConnected;
        /**
         * @brief       收到字节分块；参数为连接与只在回调期间有效的字节视图。
         */
        DataCallback onData;
        /**
         * @brief       对端关闭写方向，已读到 EOF；参数为连接。
         */
        PeerHalfCloseCallback onPeerHalfClose;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         */
        ErrorCallback onError;
        /**
         * @brief       关闭通知；对象仍须存活到相关关闭回调完成。
         */
        ClosedCallback onClosed;
        /**
         * @brief       TCP 待写量下降后的通知，可恢复受背压暂停的生产者。
         */
        WriteDrainedCallback onWriteDrained;
    };

    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TcpConnection(const TcpConnection &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TcpConnection &operator=(const TcpConnection &other) = delete;
    /**
     * @brief       释放 TcpConnection 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    ~TcpConnection();

    /**
     * @brief       查询当前状态
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      当前生命周期状态快照，可能在返回后变化。
     */
    State state() const noexcept;
    /**
     * @brief       查询已排队的待写字节数
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      当前字节数量或条目数量。
     */
    std::size_t pendingWriteBytes() const noexcept;
    /**
     * @brief       判断调用线程是否为所属事件循环线程
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isLoopThread() const noexcept override;
    /**
     * @brief       获取所属事件循环
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     */
    EventLoop &eventLoop() noexcept;
    /**
     * @brief       提交一块流式数据
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @param[in] callback 通知回调，空函数表示不接收相应通知
     * @return      Queued 已接受；Backpressured 未接受；NotConnected 当前不能写，不表示远端已经收到。
     */
    WriteResult write(std::string data, WriteCallback callback = {}) noexcept;
    /**
     * @brief       向底层传输提交字节
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @return      写入提交状态，不是远端确认；不同传输的失败映射见具体实现。
     */
    WriteStatus writeBytes(std::string data) noexcept override;
    /**
     * @brief       请求关闭字节流
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool closeStream() noexcept override
    {
        return close();
    }
    /**
     * @brief       请求写方向半关闭
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool shutdownWrite() noexcept override
    {
        return shutdown();
    }
    /**
     * @brief       设置缓冲下降后的可写通知
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @param[in] callback 通知回调，空函数表示不接收相应通知
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool setWritableCallback(WritableCallback callback) noexcept override;
    /**
     * @brief       向所属事件循环投递任务
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool post(Task task) noexcept override
    {
        return loop_.post(std::move(task));
    }
    /**
     * @brief       暂停接收回调
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool pauseRead() noexcept;
    /**
     * @brief       恢复接收回调
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool resumeRead() noexcept;
    /**
     * @brief       请求排空待写数据后关闭写方向
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool shutdown() noexcept;
    /**
     * @brief       请求关闭连接或客户端
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool close() noexcept;
    /**
     * @brief       查询远端 IP 地址
     * @threadsafe  否；在所属 loop 查询远端地址。
     * @return      独立持有的数据字符串。
     */
    std::string peerAddress() const;
    /**
     * @brief       查询远端端口
     * @threadsafe  否；在所属 loop 查询远端地址。
     * @return      端口号；尚未绑定或查询失败时为 0。
     */
    std::uint16_t peerPort() const;

  private:
    struct WriteRequest;

    TcpConnection(EventLoop &loop, Callbacks callbacks, Options options);
    static void allocateReadBuffer(uv_handle_t *handle, std::size_t suggestedSize,
                                   uv_buf_t *buffer) noexcept;
    static void onRead(uv_stream_t *stream, ssize_t bytesRead, const uv_buf_t *buffer) noexcept;
    static void onWrite(uv_write_t *request, int status) noexcept;
    static void onShutdown(uv_shutdown_t *request, int status) noexcept;
    static void onResolved(uv_getaddrinfo_t *request, int status, addrinfo *addresses) noexcept;
    static void onConnect(uv_connect_t *request, int status) noexcept;
    static void onClosed(uv_handle_t *handle) noexcept;

    bool initializeAccepted(uv_stream_t *server) noexcept;
    void startReadInLoop() noexcept;
    void beginConnectInLoop(std::string host, std::uint16_t port,
                            ConnectCallback callback) noexcept;
    void connectNextAddressInLoop() noexcept;
    void finishConnect(Error error) noexcept;
    void pauseReadInLoop() noexcept;
    void resumeReadInLoop() noexcept;
    void enqueueWriteInLoop(std::string data, WriteCallback callback) noexcept;
    void beginShutdownInLoop() noexcept;
    void closeInLoop(int errorCode = 0) noexcept;
    void notifyError(int errorCode) noexcept;
    void invoke(const std::function<void(const Ptr &)> &callback) noexcept;

    EventLoop &loop_;
    Callbacks callbacks_;
    WritableCallback writableCallback_;
    Options options_;
    uv_tcp_t handle_{};
    uv_shutdown_t shutdownRequest_{};
    uv_getaddrinfo_t resolverRequest_{};
    uv_connect_t connectRequest_{};
    std::atomic<State> state_{State::Connecting};
    std::atomic<std::size_t> pendingWriteBytes_{0};
    std::atomic<bool> closeRequested_{false};
    std::atomic<bool> shutdownRequested_{false};
    bool handleInitialized_{false};
    bool handleClosed_{false};
    bool reading_{false};
    bool peerReadEof_{false};
    bool shutdownStarted_{false};
    bool shutdownCompleted_{false};
    bool errorNotified_{false};
    Ptr keepAlive_;
    Ptr resolverKeepAlive_;
    Ptr connectKeepAlive_;
    std::string connectHost_;
    std::string connectService_;
    ConnectCallback connectCallback_;
    std::vector<sockaddr_storage> connectAddresses_;
    std::size_t nextConnectAddress_{0};
    int lastConnectError_{UV_ECONNREFUSED};
    bool resolverPending_{false};

    friend class TcpServer;
    friend class TcpClient;
};

} // namespace uvhttp

#endif
