/**
 * @file        tcp_server.h
 * @brief       TCP 监听器、连接接入及异步停止接口
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_TCP_SERVER_H
#define UVHTTP_TCP_SERVER_H

#include <atomic>

#include "uvhttp/export.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "uvhttp/tcp_connection.h"

namespace uvhttp
{

/**
 * @brief       TCP 监听器、连接接入及异步停止接口。
 */
class UVHTTP_EXPORT TcpServer : public std::enable_shared_from_this<TcpServer>
{
  public:
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<TcpServer>;

    /**
     * @brief       对象生命周期状态。
     */
    enum class State : unsigned char
    {
        /**
         * @brief       对象已创建，尚未开始监听。
         */
        Created,
        /**
         * @brief       监听启动请求已提交。
         */
        Starting,
        /**
         * @brief       已绑定并开始监听。
         */
        Listening,
        /**
         * @brief       正在关闭监听器及活动连接。
         */
        Stopping,
        /**
         * @brief       监听器及活动连接已停止。
         */
        Stopped
    };

    /**
     * @brief       初始化配置；不与对象运行状态同步修改。
     */
    struct Options
    {
        /**
         * @brief       监听套接字的待接入连接队列长度，实际受操作系统限制。
         */
        int backlog{512};
        /**
         * @brief       每个新 TCP 连接的配置。
         */
        TcpConnection::Options connection;
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
         * @brief       TCP 服务端接入连接通知；参数为新连接共享指针。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const TcpConnection::Ptr &)> onConnection;
        /**
         * @brief       收到字节分块；参数为连接与只在回调期间有效的字节视图。
         */
        TcpConnection::DataCallback onData;
        /**
         * @brief       对端关闭写方向，已读到 EOF；参数为连接。
         */
        TcpConnection::PeerHalfCloseCallback onPeerHalfClose;
        /**
         * @brief       已接入 TCP 连接的错误通知；参数为连接和 Error。
         */
        TcpConnection::ErrorCallback onConnectionError;
        /**
         * @brief       已接入 TCP 连接完成关闭；参数为连接。
         */
        TcpConnection::ClosedCallback onConnectionClosed;
        /**
         * @brief       TCP 待写量下降后的通知，可恢复受背压暂停的生产者。
         */
        TcpConnection::WriteDrainedCallback onWriteDrained;
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
    };

    /**
     * @brief       构造 TcpServer 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] address 监听地址，通常为本地 IPv4/IPv6 字面地址
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     */
    TcpServer(EventLoop &loop, std::string address, std::uint16_t port);
    /**
     * @brief       构造 TcpServer 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] address 监听地址，通常为本地 IPv4/IPv6 字面地址
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     */
    TcpServer(EventLoop &loop, std::string address, std::uint16_t port, Options options,
              Callbacks callbacks);
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TcpServer(const TcpServer &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TcpServer &operator=(const TcpServer &other) = delete;
    /**
     * @brief       释放 TcpServer 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     先 stop 并等待 onStopped，再释放最后的所有者；析构不是阻塞式等待关闭。
     */
    ~TcpServer();

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
     * @brief       查询当前状态
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      当前生命周期状态快照，可能在返回后变化。
     */
    State state() const noexcept;
    /**
     * @brief       获取实际监听端口
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      端口号；尚未绑定或查询失败时为 0。
     */
    std::uint16_t port() const noexcept;
    /**
     * @brief       查询当前连接数量
     * @threadsafe  是；对象必须存活，返回值仅反映当前状态或投递结果。
     * @return      当前字节数量或条目数量。
     */
    std::size_t connectionCount() const noexcept;

  private:
    static void onConnection(uv_stream_t *server, int status) noexcept;
    static void onListenerClosed(uv_handle_t *handle) noexcept;
    static void onConnectionClosed(const TcpConnection::Ptr &connection,
                                   std::weak_ptr<TcpServer> server,
                                   TcpConnection::ClosedCallback userCallback) noexcept;

    void startInLoop() noexcept;
    void acceptInLoop() noexcept;
    void stopInLoop() noexcept;
    void closeListenerInLoop() noexcept;
    void maybeFinishStop() noexcept;
    void notifyError(int errorCode) noexcept;

    EventLoop &loop_;
    std::string address_;
    std::uint16_t requestedPort_;
    Options options_;
    Callbacks callbacks_;
    uv_tcp_t listener_{};
    std::atomic<State> state_{State::Created};
    std::atomic<std::uint16_t> boundPort_{0};
    std::atomic<std::size_t> connectionCount_{0};
    bool listenerInitialized_{false};
    bool listenerClosed_{true};
    Ptr keepAlive_;
    std::unordered_map<TcpConnection *, TcpConnection::Ptr> connections_;
};

} // namespace uvhttp

#endif
