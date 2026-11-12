/**
 * @file http_sync_stream.h
 * @brief HTTP/1.1 和 HTTP/2 同步上传/读取外观，网络仍由独立异步事件循环驱动。
 * @author Ethan
 * @version 1.0 初次建立
 * @date 2026.11.11
 */
#ifndef UVHTTP_HTTP_SYNC_STREAM_H
#define UVHTTP_HTTP_SYNC_STREAM_H
#include "uvhttp/http_client.h"
namespace uvhttp {
/** @brief 串行复用连接的同步流式会话；读写由一个调用线程管理，cancel 可由另一线程调用，析构前须结束调用。 */
class UVHTTP_EXPORT HttpSyncStream {
public:
    enum class Protocol { Http1, Http2 };
    struct Options {
        /** @brief HTTP/1.1 或 HTTP/2 prior-knowledge；TLS 时分别使用 http/1.1 与 h2。 */
        Protocol protocol{Protocol::Http1};
        /** @brief 包括用户读写等待在内的请求总期限，必须大于零。 */
        std::chrono::milliseconds timeout{60000};
        /** @brief 缓冲高水位，至少64 KiB；到达后暂停专用连接读，低水位恢复。 */
        std::size_t highWaterBytes{256*1024};
        /** @brief 连接池容量、空闲回收等配置；强制单并发请求，避免暂停读影响其他流。 */
        ConnectionPoolOptions pool;
    };
    /** @brief 构造 HTTP/1.1 流式会话。 */
    HttpSyncStream();
    /** @brief 构造指定协议会话，非法配置抛出 invalid_argument。 */
    explicit HttpSyncStream(Options options);
    /** @brief 取消请求并关闭线程；不得与 read/write/begin 并发析构。 */
    ~HttpSyncStream();
    HttpSyncStream(const HttpSyncStream &) = delete;
    HttpSyncStream &operator=(const HttpSyncStream &) = delete;
    /** @brief 开始明文流式上传，等待上传就绪；需调用 endUpload，完成读取后才可开始下一请求。 */
    bool begin(std::string host,std::uint16_t port,HttpRequestHead request,Error &error);
    /** @brief 开始 TLS 流式上传；context 非空，不并发修改；默认校验证书。 */
    bool beginTls(std::string host,std::uint16_t port,SSL_CTX *context,HttpRequestHead request,Error &error,bool verifyPeer=true);
    /** @brief 写入一个分块，最多64 KiB；返回 true 表示接受而非对端已收，EAGAIN 时可重试原数据。 */
    bool write(std::string bytes,Error &error);
    /** @brief 提交上传结束；仍需 readHead/read 等待响应。 */
    bool endUpload(Error &error);
    /** @brief 等待最终响应头，中间1xx不作为最终头返回。 */
    bool readHead(HttpResponseHead &head,Error &error);
    /** @brief 阻塞读取最多capacity字节；0且error为空表示EOF，0且error非空表示失败；capacity须大于零。 */
    std::size_t read(char *destination,std::size_t capacity,Error &error);
    /** @brief 取消本会话请求并唤醒等待；关闭连接池，此实例取消后不再接受请求。线程安全。 */
    void cancel() noexcept;
    /** @brief 查询内部池统计快照。 */
    ConnectionPoolStats poolStats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
