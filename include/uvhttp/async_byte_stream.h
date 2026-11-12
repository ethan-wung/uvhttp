/**
 * @file        async_byte_stream.h
 * @brief       异步字节流抽象、写入状态及可写通知接口
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_ASYNC_BYTE_STREAM_H
#define UVHTTP_ASYNC_BYTE_STREAM_H

#include <functional>
#include <memory>
#include <string>

#include "uvhttp/error.h"
#include "uvhttp/export.h"

namespace uvhttp
{

/**
 * @brief       异步字节流抽象、写入状态及可写通知接口。
 */
class UVHTTP_EXPORT AsyncByteStream
{
  public:
    /**
     * @brief       字节写入提交状态，不是对端应用确认。
     */
    enum class WriteStatus : unsigned char
    {
        /**
         * @brief       数据已被接受，尚不代表对端收到。
         */
        Accepted,
        /**
         * @brief       暂时不能接受更多写入；TLS 失败也可能映射到此值。
         */
        Backpressured,
        /**
         * @brief       连接/字节流已关闭。
         */
        Closed
    };
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<AsyncByteStream>;
    /**
     * @brief       接收 Error 的操作完成通知。
     */
    using Completion = std::function<void(const Error &)>;
    /**
     * @brief       无参数可写通知，供恢复生产者使用。
     */
    using WritableCallback = std::function<void()>;
    /**
     * @brief       无参数任务函数；事件循环任务不得阻塞或抛出未处理异常。
     */
    using Task = std::function<void()>;

    /**
     * @brief       释放 AsyncByteStream 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    virtual ~AsyncByteStream() = default;
    /**
     * @brief       向底层传输提交字节
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @param[in] bytes 本次输入字节；string_view 只在调用期间有效，底层按接口语义消费/复制
     * @return      写入提交状态，不是远端确认；不同传输的失败映射见具体实现。
     */
    virtual WriteStatus writeBytes(std::string bytes) noexcept = 0;
    /**
     * @brief       请求关闭字节流
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    virtual bool closeStream() noexcept = 0;
    /**
     * @brief       请求写方向半关闭
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    virtual bool shutdownWrite() noexcept = 0;
    /**
     * @brief       设置缓冲下降后的可写通知
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @param[in] callback 通知回调，空函数表示不接收相应通知
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    virtual bool setWritableCallback(WritableCallback callback) noexcept = 0;
    /**
     * @brief       判断调用线程是否为所属事件循环线程
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    virtual bool isLoopThread() const noexcept = 0;
    /**
     * @brief       向所属事件循环投递任务
     * @threadsafe  由具体实现决定；通用调用方应在所属 loop 调用，跨线程使用 post。
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    virtual bool post(Task task) noexcept = 0;
};

} // namespace uvhttp

#endif
