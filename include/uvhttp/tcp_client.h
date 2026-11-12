/**
 * @file        tcp_client.h
 * @brief       基于 libuv 异步 DNS 和连接建立的 TCP 客户端
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_TCP_CLIENT_H
#define UVHTTP_TCP_CLIENT_H

#include <cstdint>
#include <string>

#include "uvhttp/tcp_connection.h"

namespace uvhttp
{

/**
 * @brief       基于 libuv 异步 DNS 和连接建立的 TCP 客户端。
 */
class UVHTTP_EXPORT TcpClient
{
  public:
    /**
     * @brief       构造 TcpClient 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     */
    explicit TcpClient(EventLoop &loop, TcpConnection::Options options = TcpConnection::Options{});

    /**
     * @brief       获取所属事件循环
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     */
    EventLoop &eventLoop() noexcept
    {
        return loop_;
    }

    /**
     * @brief       异步建立连接
     * @threadsafe  是；允许跨线程提交，所属 loop 和客户端须保持存活，不得与析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] connected TCP 建连结果回调，在 loop 执行并提供连接及错误
     * @param[out] pendingConnection 可选输出，取得尚在连接阶段的 TCP 对象，用于提前关闭
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool connect(std::string host, std::uint16_t port, TcpConnection::Callbacks callbacks,
                 TcpConnection::ConnectCallback connected,
                 TcpConnection::Ptr *pendingConnection = nullptr) noexcept;

  private:
    EventLoop &loop_;
    TcpConnection::Options options_;
};

} // namespace uvhttp

#endif
