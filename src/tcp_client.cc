/**
 * @file        tcp_client.cc
 * @brief       基于 libuv 异步 DNS 和连接建立的 TCP 客户端的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/tcp_client.h"

#include <utility>

namespace uvhttp
{

TcpClient::TcpClient(EventLoop &loop, TcpConnection::Options options)
    : loop_(loop), options_(options)
{
}

bool TcpClient::connect(std::string host,
                        std::uint16_t port,
                        TcpConnection::Callbacks callbacks,
                        TcpConnection::ConnectCallback connected,
                        TcpConnection::Ptr *pendingConnection) noexcept
{
    auto connection = TcpConnection::Ptr(
        new TcpConnection(loop_, std::move(callbacks), options_));
    if (pendingConnection) *pendingConnection = connection;
    return loop_.post([connection,
                       host = std::move(host),
                       port,
                       connected = std::move(connected)]() mutable {
        connection->beginConnectInLoop(std::move(host), port, std::move(connected));
    });
}

}  // namespace uvhttp
