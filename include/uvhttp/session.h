/**
 * @file        session.h
 * @brief       带有效期和读写锁的内存会话键值存储
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_SESSION_H
#define UVHTTP_SESSION_H
#include <chrono>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "uvhttp/export.h"
namespace uvhttp
{
/**
 * @brief       带有效期和读写锁的内存会话键值存储。
 */
class UVHTTP_EXPORT SessionStore
{
  public:
    /**
     * @brief       会话字符串键值表。
     */
    using Values = std::unordered_map<std::string, std::string>;
    /**
     * @brief       构造 SessionStore 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] ttl 会话有效期，默认 24 小时
     */
    explicit SessionStore(std::chrono::seconds ttl = std::chrono::hours(24));
    /**
     * @brief       创建带有效期的内存会话
     * @threadsafe  是；内部读写锁保护存储，传入/传出对象不应被其他线程并发修改。
     * @param[in] values 会话字符串键值表
     * @return      独立持有的数据字符串。
     * @details     会话 ID 不是密码学随机令牌，不应直接用作生产认证凭证；本存储不自动绑定 Cookie。
     */
    std::string create(Values values = {});
    /**
     * @brief       取得尚未过期会话的键值副本
     * @threadsafe  是；内部读写锁保护存储，传入/传出对象不应被其他线程并发修改。
     * @param[in] id 目标条目的标识
     * @param[out] values 会话字符串键值表
     * @return      存在且未过期时为 true 并输出副本，否则 false，输出不保证被清空。
     */
    bool get(const std::string &id, Values &values) const;
    /**
     * @brief       替换已有会话的值并刷新有效期
     * @threadsafe  是；内部读写锁保护存储，传入/传出对象不应被其他线程并发修改。
     * @param[in] id 目标条目的标识
     * @param[in] values 会话字符串键值表
     * @return      键存在时为 true 并刷新有效期；不存在时 false（当前实现允许刷新已过期但未清理的键）。
     */
    bool set(const std::string &id, Values values);
    /**
     * @brief       删除指定会话
     * @threadsafe  是；内部读写锁保护存储，传入/传出对象不应被其他线程并发修改。
     * @param[in] id 目标条目的标识
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool erase(const std::string &id);
    /**
     * @brief       回收所有已过期会话
     * @threadsafe  是；内部读写锁保护存储，传入/传出对象不应被其他线程并发修改。
     * @return      无。
     */
    void purge();

  private:
    struct Entry
    {
        Values values;
        std::chrono::steady_clock::time_point expires;
    };
    std::chrono::seconds ttl_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> sessions_;
};
} // namespace uvhttp
#endif
