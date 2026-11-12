/**
 * @file        tls_stream.h
 * @brief       基于 OpenSSL 的异步 TLS 字节流及 ALPN 协商
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_TLS_STREAM_H
#define UVHTTP_TLS_STREAM_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/ssl.h>

#include "uvhttp/async_byte_stream.h"
#include "uvhttp/export.h"
#include "uvhttp/tcp_client.h"

namespace uvhttp
{

/**
 * @brief       基于 OpenSSL 的异步 TLS 字节流及 ALPN 协商。
 */
class UVHTTP_EXPORT TlsStream : public AsyncByteStream,
                                public std::enable_shared_from_this<TlsStream>
{
  public:
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<TlsStream>;
    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       TLS 握手成功；参数为 TLS 连接，可读取 negotiatedAlpn。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &)> onHandshake;
        /**
         * @brief       收到字节分块；参数为连接与只在回调期间有效的字节视图。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, std::string_view)> onData;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
        /**
         * @brief       关闭通知；对象仍须存活到相关关闭回调完成。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void()> onClosed;
    };

    /**
     * @brief       为已接入的 TCP 连接创建服务端 TLS 层
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] connection 已接入的 TCP 连接，需由调用者转发加密数据和关闭事件
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] alpnProtocols 期望协商的 ALPN 协议列表，按调用方提供的顺序传入
     * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
     */
    static Ptr accept(TcpConnection::Ptr connection, SSL_CTX *context, Callbacks callbacks,
                      std::vector<std::string> alpnProtocols = {});
    /**
     * @brief       异步建立连接
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] client 用于发起底层 TCP 建连的客户端，须使用同一事件循环
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] alpnProtocols 期望协商的 ALPN 协议列表，按调用方提供的顺序传入
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @param[out] pendingConnection 可选输出，取得尚在连接阶段的 TCP 对象，用于提前关闭
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    static bool connect(EventLoop &loop, TcpClient &client, std::string host, std::uint16_t port,
                        SSL_CTX *context, Callbacks callbacks,
                        std::vector<std::string> alpnProtocols = {}, bool verifyPeer = true,
                        TcpConnection::Ptr *pendingConnection = nullptr);

    /**
     * @brief       释放 TlsStream 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    ~TlsStream();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TlsStream(const TlsStream &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    TlsStream &operator=(const TlsStream &other) = delete;

    /**
     * @brief       提交一块流式数据
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool write(std::string data) noexcept;
    /**
     * @brief       向底层传输提交字节
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @return      当前实现 write 失败统一映射为 Backpressured，不能据此区分连接已关闭。
     */
    WriteStatus writeBytes(std::string data) noexcept override;
    /**
     * @brief       请求关闭字节流
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      当前实现调用 close 后固定返回 true，不表示对端收到关闭通知。
     */
    bool closeStream() noexcept override
    {
        close();
        return true;
    }
    /**
     * @brief       请求写方向半关闭
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool shutdownWrite() noexcept override;
    /**
     * @brief       设置缓冲下降后的可写通知
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] callback 通知回调，空函数表示不接收相应通知
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool setWritableCallback(WritableCallback callback) noexcept override;
    /**
     * @brief       判断调用线程是否为所属事件循环线程
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isLoopThread() const noexcept override;
    /**
     * @brief       向所属事件循环投递任务
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool post(Task task) noexcept override;
    /**
     * @brief       请求关闭连接或客户端
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      无。
     */
    void close() noexcept;
    /**
     * @brief       查询 TLS 握手是否已经完成
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool handshakeComplete() const noexcept;
    /**
     * @brief       获取 TLS 协商出的 ALPN 协议
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      独立持有的数据字符串。
     */
    std::string negotiatedAlpn() const;
    /**
     * @brief       取得 TLS 层使用的 TCP 连接
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
     */
    TcpConnection::Ptr tcpConnection() const;

    /**
     * @brief       向服务端 TLS 层提交加密网络数据
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @param[in] bytes 本次输入字节；string_view 只在调用期间有效，底层按接口语义消费/复制
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool feedEncrypted(std::string_view bytes) noexcept;
    /**
     * @brief       通知 TLS 层对端已关闭连接
     * @threadsafe  否；除 post/close 的投递用途外，在所属 loop 调用，避免与关闭状态竞争。
     * @return      无。
     */
    void peerClosed() noexcept;

  private:
    TlsStream(TcpConnection::Ptr connection, SSL_CTX *context, bool server, Callbacks callbacks,
              std::vector<std::string> alpnProtocols, std::string peerHost = {},
              bool verifyPeer = true);
    bool initialize() noexcept;
    bool writeInLoop(std::string data) noexcept;
    void processEncrypted(std::string_view bytes) noexcept;
    void driveHandshake() noexcept;
    void drainEncrypted() noexcept;
    void flushEncrypted() noexcept;
    void readPlaintext() noexcept;
    void fail(std::string message, int code = 0) noexcept;
    static int selectAlpn(SSL *ssl, const unsigned char **out, unsigned char *outLength,
                          const unsigned char *input, unsigned int inputLength,
                          void *argument) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uvhttp

#endif
