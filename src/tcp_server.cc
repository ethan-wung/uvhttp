/**
 * @file        tcp_server.cc
 * @brief       TCP 监听器、连接接入及异步停止接口的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/tcp_server.h"

#include <cstring>
#include <utility>

namespace uvhttp
{

TcpServer::TcpServer(EventLoop &loop, std::string address, std::uint16_t port)
    : TcpServer(loop, std::move(address), port, Options{}, Callbacks{})
{
}
TcpServer::TcpServer(EventLoop &loop,
                     std::string address,
                     std::uint16_t port,
                     Options options,
                     Callbacks callbacks)
    : loop_(loop),
      address_(std::move(address)),
      requestedPort_(port),
      options_(options),
      callbacks_(std::move(callbacks))
{
}

TcpServer::~TcpServer()
{
    if (listenerInitialized_ && !listenerClosed_)
    {
        std::terminate();
    }
}

bool TcpServer::start() noexcept
{
    State expected = State::Created;
    if (!state_.compare_exchange_strong(expected, State::Starting, std::memory_order_acq_rel))
    {
        return false;
    }
    Ptr self = shared_from_this();
    if (!loop_.post([self] { self->startInLoop(); }))
    {
        state_.store(State::Created, std::memory_order_release);
        return false;
    }
    return true;
}

bool TcpServer::stop() noexcept
{
    State current = state_.load(std::memory_order_acquire);
    while (current != State::Stopped && current != State::Stopping)
    {
        if (state_.compare_exchange_weak(current, State::Stopping, std::memory_order_acq_rel))
        {
            Ptr self = shared_from_this();
            if (loop_.post([self] { self->stopInLoop(); }))
            {
                return true;
            }
            State stopping = State::Stopping;
            state_.compare_exchange_strong(stopping, current, std::memory_order_acq_rel);
            return false;
        }
    }
    return false;
}
TcpServer::State TcpServer::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

std::uint16_t TcpServer::port() const noexcept
{
    return boundPort_.load(std::memory_order_acquire);
}

std::size_t TcpServer::connectionCount() const noexcept
{
    return connectionCount_.load(std::memory_order_acquire);
}

void TcpServer::onConnection(uv_stream_t *server, int status) noexcept
{
    auto *tcpServer = static_cast<TcpServer *>(server->data);
    if (status < 0)
    {
        tcpServer->notifyError(status);
        return;
    }
    tcpServer->acceptInLoop();
}

void TcpServer::onListenerClosed(uv_handle_t *handle) noexcept
{
    auto *server = static_cast<TcpServer *>(handle->data);
    server->listenerClosed_ = true;
    server->maybeFinishStop();
}

void TcpServer::onConnectionClosed(const TcpConnection::Ptr &connection,
                                   std::weak_ptr<TcpServer> weakServer,
                                   TcpConnection::ClosedCallback userCallback) noexcept
{
    if (userCallback)
    {
        if (auto server = weakServer.lock())
        {
            try
            {
                userCallback(connection);
            }
            catch (...)
            {
                server->loop_.reportException(std::current_exception());
            }
        }
    }
    if (auto server = weakServer.lock())
    {
        server->connections_.erase(connection.get());
        server->connectionCount_.store(server->connections_.size(), std::memory_order_release);
        server->maybeFinishStop();
    }
}

void TcpServer::startInLoop() noexcept
{
    if (state() != State::Starting)
    {
        maybeFinishStop();
        return;
    }
    keepAlive_ = shared_from_this();
    int status = uv_tcp_init(&loop_.loop_, &listener_);
    if (status < 0)
    {
        state_.store(State::Stopping, std::memory_order_release);
        listenerClosed_ = true;
        notifyError(status);
        maybeFinishStop();
        return;
    }
    listenerInitialized_ = true;
    listenerClosed_ = false;
    listener_.data = this;

    sockaddr_storage address{};
    if (address_.find(':') != std::string::npos)
    {
        status = uv_ip6_addr(address_.c_str(), requestedPort_,
                             reinterpret_cast<sockaddr_in6 *>(&address));
    }
    else
    {
        status = uv_ip4_addr(address_.c_str(), requestedPort_,
                             reinterpret_cast<sockaddr_in *>(&address));
    }
    if (status == 0)
    {
        status = uv_tcp_bind(&listener_, reinterpret_cast<const sockaddr *>(&address), 0);
    }
    if (status == 0)
    {
        status = uv_listen(reinterpret_cast<uv_stream_t *>(&listener_),
                           options_.backlog,
                           &TcpServer::onConnection);
    }
    if (status < 0)
    {
        state_.store(State::Stopping, std::memory_order_release);
        notifyError(status);
        closeListenerInLoop();
        return;
    }

    sockaddr_storage localAddress{};
    int addressLength = sizeof(localAddress);
    status = uv_tcp_getsockname(&listener_,
                                reinterpret_cast<sockaddr *>(&localAddress),
                                &addressLength);
    if (status == 0)
    {
        if (localAddress.ss_family == AF_INET)
        {
            boundPort_.store(ntohs(reinterpret_cast<sockaddr_in *>(&localAddress)->sin_port),
                             std::memory_order_release);
        }
        else if (localAddress.ss_family == AF_INET6)
        {
            boundPort_.store(ntohs(reinterpret_cast<sockaddr_in6 *>(&localAddress)->sin6_port),
                             std::memory_order_release);
        }
    }
    state_.store(State::Listening, std::memory_order_release);
    if (callbacks_.onListening)
    {
        try
        {
            callbacks_.onListening(port());
        }
        catch (...)
        {
            loop_.reportException(std::current_exception());
            stopInLoop();
        }
    }
}

void TcpServer::acceptInLoop() noexcept
{
    if (state() != State::Listening)
    {
        return;
    }
    TcpConnection::Callbacks connectionCallbacks;
    connectionCallbacks.onData = callbacks_.onData;
    connectionCallbacks.onPeerHalfClose = callbacks_.onPeerHalfClose;
    connectionCallbacks.onError = callbacks_.onConnectionError;
    connectionCallbacks.onWriteDrained = callbacks_.onWriteDrained;
    std::weak_ptr<TcpServer> weakServer = keepAlive_;
    connectionCallbacks.onClosed = [weakServer, userCallback = callbacks_.onConnectionClosed](
                                       const TcpConnection::Ptr &connection) {
        TcpServer::onConnectionClosed(connection, weakServer, userCallback);
    };

    auto connection = TcpConnection::Ptr(
        new TcpConnection(loop_, std::move(connectionCallbacks), options_.connection));
    if (!connection->initializeAccepted(reinterpret_cast<uv_stream_t *>(&listener_)))
    {
        if (!connection->handleInitialized_)
        {
            notifyError(UV_ENOMEM);
        }
        return;
    }
    connections_.emplace(connection.get(), connection);
    connectionCount_.store(connections_.size(), std::memory_order_release);
    if (callbacks_.onConnection)
    {
        try
        {
            callbacks_.onConnection(connection);
        }
        catch (...)
        {
            loop_.reportException(std::current_exception());
            connection->closeInLoop(UV_EFAULT);
        }
    }
}

void TcpServer::stopInLoop() noexcept
{
    State current = state();
    if (current == State::Stopped)
    {
        return;
    }
    state_.store(State::Stopping, std::memory_order_release);
    closeListenerInLoop();
    for (auto &entry : connections_)
    {
        entry.second->closeInLoop();
    }
    maybeFinishStop();
}

void TcpServer::closeListenerInLoop() noexcept
{
    if (!listenerInitialized_ || listenerClosed_ ||
        uv_is_closing(reinterpret_cast<uv_handle_t *>(&listener_)))
    {
        listenerClosed_ = true;
        maybeFinishStop();
        return;
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&listener_), &TcpServer::onListenerClosed);
}

void TcpServer::maybeFinishStop() noexcept
{
    if (state() != State::Stopping || !listenerClosed_ || !connections_.empty())
    {
        return;
    }
    state_.store(State::Stopped, std::memory_order_release);
    if (callbacks_.onStopped)
    {
        try
        {
            callbacks_.onStopped();
        }
        catch (...)
        {
            loop_.reportException(std::current_exception());
        }
    }
    keepAlive_.reset();
}

void TcpServer::notifyError(int errorCode) noexcept
{
    if (!callbacks_.onError)
    {
        return;
    }
    try
    {
        callbacks_.onError(Error::fromUv(errorCode));
    }
    catch (...)
    {
        loop_.reportException(std::current_exception());
    }
}

}  // namespace uvhttp
