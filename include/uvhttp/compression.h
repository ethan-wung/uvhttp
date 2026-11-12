/**
 * @file        compression.h
 * @brief       增量压缩接口及 identity、gzip、Brotli 后端工厂
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_COMPRESSION_H
#define UVHTTP_COMPRESSION_H
#include <memory>
#include <string>
#include <string_view>
#include "uvhttp/export.h"
namespace uvhttp
{
/**
 * @brief       增量压缩接口及 identity、gzip、Brotli 后端工厂。
 */
class UVHTTP_EXPORT CompressionCodec
{
  public:
    /**
     * @brief       释放 CompressionCodec 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    virtual ~CompressionCodec() = default;
    /**
     * @brief       查询压缩编码名称
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      借用的数据视图/地址；对象修改或销毁后可能失效。
     */
    virtual std::string_view name() const noexcept = 0;
    /**
     * @brief       增量压缩输入并追加到输出缓冲区
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] input 本次待压缩输入，视图在调用期间保持有效
     * @param[in,out] output 追加压缩结果的字符串，不自动清空已有内容
     * @param[in] finish 是否为最后一块输入；true 尝试结束当前压缩流
     * @return      压缩成功为 true；失败为 false，output 可能已追加部分数据。
     */
    virtual bool compress(std::string_view input, std::string &output, bool finish = true) = 0;
    /**
     * @brief       重置状态以处理下一条数据流
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    virtual bool reset() = 0;
};
/**
 * @brief       创建不压缩、仅复制输入的编码器
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
 */
UVHTTP_EXPORT std::unique_ptr<CompressionCodec> createIdentityCodec();
/**
 * @brief       创建 gzip 增量编码器
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
 */
UVHTTP_EXPORT std::unique_ptr<CompressionCodec> createGzipCodec();
/**
 * @brief       创建 Brotli 增量编码器
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
 */
UVHTTP_EXPORT std::unique_ptr<CompressionCodec> createBrotliCodec();
} // namespace uvhttp
#endif
