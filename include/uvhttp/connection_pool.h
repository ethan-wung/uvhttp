/**
 * @file        connection_pool.h
 * @brief       客户端连接池的容量、超时配置与统计快照
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_CONNECTION_POOL_H
#define UVHTTP_CONNECTION_POOL_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include "uvhttp/export.h"

namespace uvhttp
{
/**
 * @brief       连接池限额与超时配置；构造客户端时读取。
 */
struct UVHTTP_EXPORT ConnectionPoolOptions
{
    /**
     * @brief       连接池 origin 数量上限，必须大于零。
     */
    std::size_t maxOrigins{64};
    /**
     * @brief       全池连接数量上限，必须大于零。
     */
    std::size_t maxConnections{128};
    /**
     * @brief       每个 origin 的连接数量上限，必须大于零。
     */
    std::size_t maxConnectionsPerOrigin{2};
    /**
     * @brief       每个 origin 尚未分配连接/流的排队请求上限，必须大于零。
     */
    std::size_t maxPendingRequestsPerOrigin{128};
    /**
     * @brief       全池未完成请求上限，包含排队和活动请求，必须大于零。
     */
    std::size_t maxOutstandingRequests{4096};
    /**
     * @brief       建连阶段超时，单位毫秒；零禁用，负数非法。
     */
    std::chrono::milliseconds connectTimeout{10000};
    /**
     * @brief       从提交开始的请求总期限（含排队），单位毫秒；零禁用，负数非法。
     */
    std::chrono::milliseconds requestTimeout{60000};
    /**
     * @brief       空闲连接回收期限，单位毫秒；零禁用，负数非法。
     */
    std::chrono::milliseconds idleTimeout{30000};
    /**
     * @brief       池维护扫描周期，单位毫秒，必须大于零。
     */
    std::chrono::milliseconds maintenanceInterval{100};
};

/**
 * @brief       连接池状态及累计计数的只读用途快照。
 */
struct UVHTTP_EXPORT ConnectionPoolStats
{
    /**
     * @brief       当前 origin 数量。
     */
    std::size_t origins{0};
    /**
     * @brief       当前池连接数量。
     */
    std::size_t connections{0};
    /**
     * @brief       正在建立的连接数量。
     */
    std::size_t connecting{0};
    /**
     * @brief       当前空闲连接数量。
     */
    std::size_t idle{0};
    /**
     * @brief       当前等待调度的请求数量。
     */
    std::size_t pending{0};
    /**
     * @brief       当前活动请求/流数量。
     */
    std::size_t active{0};
    /**
     * @brief       当前尚未结束的提交数量，原子读取，可能与其他快照项存在时序差。
     */
    std::size_t outstanding{0};
    /**
     * @brief       累计接受的提交数量。
     */
    std::uint64_t accepted{0};
    /**
     * @brief       累计拒绝的请求数量，包括提交和排队阶段拒绝。
     */
    std::uint64_t rejected{0};
    /**
     * @brief       累计成功完成的请求数量。
     */
    std::uint64_t completed{0};
    /**
     * @brief       累计失败结束的请求数量。
     */
    std::uint64_t failed{0};
    /**
     * @brief       累计请求超时数量。
     */
    std::uint64_t timedOut{0};
    /**
     * @brief       累计创建的连接数量。
     */
    std::uint64_t connectionsCreated{0};
    /**
     * @brief       累计在已使用过的连接上分配请求的次数。
     */
    std::uint64_t reused{0};
    /**
     * @brief       累计按空闲期限回收的连接数量。
     */
    std::uint64_t idleEvictions{0};
    /**
     * @brief       累计为释放总容量而回收的其他 origin 空闲连接数量。
     */
    std::uint64_t capacityEvictions{0};
    /**
     * @brief       累计因 GOAWAY 迁移并有限重试的请求次数。
     */
    std::uint64_t goawayRetries{0};
};
} // namespace uvhttp
#endif
