/**
 * @file        buffer.h
 * @brief       可紧凑整理的字节缓冲区，用于增量协议解析
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_BUFFER_H
#define UVHTTP_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

#include "uvhttp/export.h"

namespace uvhttp
{

/**
 * @brief       可紧凑整理的字节缓冲区，用于增量协议解析。
 */
class UVHTTP_EXPORT Buffer
{
  public:
    /**
     * @brief       构造 Buffer 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] initialCapacity 初始缓冲区容量，单位字节
     */
    explicit Buffer(std::size_t initialCapacity = 4096);

    /**
     * @brief       获取可读区起始地址
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      借用的数据视图/地址；对象修改或销毁后可能失效。
     */
    const char *readableData() const noexcept;
    /**
     * @brief       获取可读字节数
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      当前字节数量或条目数量。
     */
    std::size_t readableBytes() const noexcept;
    /**
     * @brief       获取尾部可写空间大小
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      当前字节数量或条目数量。
     */
    std::size_t writableBytes() const noexcept;
    /**
     * @brief       复制追加字节到缓冲区
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @param[in] length 字节数量，不是字符数量
     * @return      无。
     * @details     输入不得与可能重新分配的内部存储重叠；扩容可能抛出分配或长度异常。
     */
    void append(const void *data, std::size_t length);
    /**
     * @brief       复制追加字节到缓冲区
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] data 待追加或发送的数据；按值字符串可移动，指针数据在调用期间须有效
     * @return      无。
     * @details     输入不得与可能重新分配的内部存储重叠；扩容可能抛出分配或长度异常。
     */
    void append(const std::string &data);
    /**
     * @brief       消费指定长度的可读数据
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] length 字节数量，不是字符数量
     * @return      无。
     * @throws      std::out_of_range length 超过当前 readableBytes。
     */
    void retrieve(std::size_t length);
    /**
     * @brief       清空可读数据并复用已分配空间
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      无。
     */
    void retrieveAll() noexcept;
    /**
     * @brief       取出并消费指定长度的数据
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] length 字节数量，不是字符数量
     * @return      独立持有的数据字符串。
     * @throws      std::out_of_range length 超过当前 readableBytes。
     */
    std::string retrieveAsString(std::size_t length);
    /**
     * @brief       取出并消费全部可读数据
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      独立持有的数据字符串。
     */
    std::string retrieveAllAsString();

  private:
    void ensureWritable(std::size_t length);

    std::vector<char> storage_;
    std::size_t readIndex_{0};
    std::size_t writeIndex_{0};
};

} // namespace uvhttp

#endif
