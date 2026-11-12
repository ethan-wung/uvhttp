/**
 * @file        tcp_connection.cc
 * @brief       事件循环所属的 TCP 连接、流式收发及背压控制的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/tcp_connection.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace uvhttp
{

struct TcpConnection::WriteRequest
{
    uv_write_t request{};
    Ptr connection;
    std::string payload;
    WriteCallback callback;
};

TcpConnection::TcpConnection(EventLoop &loop, Callbacks callbacks, Options options)
    : loop_(loop), callbacks_(std::move(callbacks)), options_(options)
{
}

TcpConnection::~TcpConnection()
{
    if (handleInitialized_ && !handleClosed_)
    {
        std::terminate();
    }
}

TcpConnection::State TcpConnection::state() const noexcept
{
    return state_.load(std::memory_order_acquire);
}

std::size_t TcpConnection::pendingWriteBytes() const noexcept
{
    return pendingWriteBytes_.load(std::memory_order_acquire);
}

bool TcpConnection::isLoopThread() const noexcept
{
    return loop_.isLoopThread();
}

EventLoop &TcpConnection::eventLoop() noexcept
{
    return loop_;
}

TcpConnection::WriteResult TcpConnection::write(std::string data,
                                                 WriteCallback callback) noexcept
{
    if (state() != State::Connected || closeRequested_.load(std::memory_order_acquire))
    {
        return WriteResult::NotConnected;
    }
    if (data.empty())
    {
        return WriteResult::Queued;
    }
    if (data.size() > std::numeric_limits<unsigned int>::max())
    {
        return WriteResult::Backpressured;
    }

    std::size_t current = pendingWriteBytes_.load(std::memory_order_relaxed);
    while (true)
    {
        if (data.size() > options_.maxPendingWriteBytes ||
            current > options_.maxPendingWriteBytes - data.size())
        {
            return WriteResult::Backpressured;
        }
        if (pendingWriteBytes_.compare_exchange_weak(current,
                                                     current + data.size(),
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_relaxed))
        {
            break;
        }
    }

    if (state() != State::Connected || closeRequested_.load(std::memory_order_acquire))
    {
        pendingWriteBytes_.fetch_sub(data.size(), std::memory_order_acq_rel);
        return WriteResult::NotConnected;
    }

    const std::size_t payloadSize = data.size();
    if (loop_.isLoopThread())
    {
        enqueueWriteInLoop(std::move(data), std::move(callback));
        return WriteResult::Queued;
    }
    Ptr self = shared_from_this();
    bool posted = loop_.post([self, data = std::move(data), callback = std::move(callback)]() mutable {
        self->enqueueWriteInLoop(std::move(data), std::move(callback));
    });
    if (!posted)
    {
        pendingWriteBytes_.fetch_sub(payloadSize, std::memory_order_acq_rel);
        return WriteResult::NotConnected;
    }
    return WriteResult::Queued;
}

AsyncByteStream::WriteStatus TcpConnection::writeBytes(std::string data) noexcept
{
    auto result = write(std::move(data));
    switch (result)
    {
        case WriteResult::Queued: return WriteStatus::Accepted;
        case WriteResult::Backpressured: return WriteStatus::Backpressured;
        default: return WriteStatus::Closed;
    }
}
bool TcpConnection::setWritableCallback(WritableCallback callback) noexcept
{
    auto set = [self = shared_from_this(), callback = std::move(callback)]() mutable {
        self->writableCallback_ = std::move(callback);
    };
    if (isLoopThread()) { set(); return true; }
    return loop_.post(std::move(set));
}
bool TcpConnection::pauseRead() noexcept
{
    Ptr self = shared_from_this();
    if (loop_.isLoopThread())
    {
        pauseReadInLoop();
        return true;
    }
    return loop_.post([self] { self->pauseReadInLoop(); });
}

bool TcpConnection::resumeRead() noexcept
{
    Ptr self = shared_from_this();
    if (loop_.isLoopThread())
    {
        resumeReadInLoop();
        return true;
    }
    return loop_.post([self] { self->resumeReadInLoop(); });
}

bool TcpConnection::shutdown() noexcept
{
    bool expected = false;
    if (!shutdownRequested_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;
    }
    State connected = State::Connected;
    if (!state_.compare_exchange_strong(connected,
                                        State::Disconnecting,
                                        std::memory_order_acq_rel))
    {
        shutdownRequested_.store(false, std::memory_order_release);
        return false;
    }
    Ptr self = shared_from_this();
    if (loop_.isLoopThread())
    {
        beginShutdownInLoop();
        return true;
    }
    if (!loop_.post([self] { self->beginShutdownInLoop(); }))
    {
        shutdownRequested_.store(false, std::memory_order_release);
        state_.store(State::Connected, std::memory_order_release);
        return false;
    }
    return true;
}

bool TcpConnection::close() noexcept
{
    State current = state_.load(std::memory_order_acquire);
    while (current != State::Closed && current != State::Disconnecting)
    {
        if (state_.compare_exchange_weak(current,
                                         State::Disconnecting,
                                         std::memory_order_acq_rel))
        {
            break;
        }
    }
    if (current == State::Closed)
    {
        return false;
    }
    bool expected = false;
    if (!closeRequested_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return false;
    }
    Ptr self = shared_from_this();
    if (loop_.isLoopThread())
    {
        closeInLoop();
        return true;
    }
    return loop_.post([self] { self->closeInLoop(); });
}

std::string TcpConnection::peerAddress() const
{
    if (!loop_.isLoopThread() || !handleInitialized_ || state() == State::Closed)
    {
        return {};
    }
    sockaddr_storage address{};
    int length = sizeof(address);
    if (uv_tcp_getpeername(const_cast<uv_tcp_t *>(&handle_),
                           reinterpret_cast<sockaddr *>(&address),
                           &length) != 0)
    {
        return {};
    }
    char text[INET6_ADDRSTRLEN]{};
    if (address.ss_family == AF_INET)
    {
        uv_ip4_name(reinterpret_cast<const sockaddr_in *>(&address), text, sizeof(text));
    }
    else if (address.ss_family == AF_INET6)
    {
        uv_ip6_name(reinterpret_cast<const sockaddr_in6 *>(&address), text, sizeof(text));
    }
    return text;
}

std::uint16_t TcpConnection::peerPort() const
{
    if (!loop_.isLoopThread() || !handleInitialized_ || state() == State::Closed)
    {
        return 0;
    }
    sockaddr_storage address{};
    int length = sizeof(address);
    if (uv_tcp_getpeername(const_cast<uv_tcp_t *>(&handle_),
                           reinterpret_cast<sockaddr *>(&address),
                           &length) != 0)
    {
        return 0;
    }
    if (address.ss_family == AF_INET)
    {
        return ntohs(reinterpret_cast<const sockaddr_in *>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_port);
    }
    return 0;
}

void TcpConnection::allocateReadBuffer(uv_handle_t *,
                                      std::size_t suggestedSize,
                                      uv_buf_t *buffer) noexcept
{
    const std::size_t length = std::min<std::size_t>(suggestedSize,
                                                      std::numeric_limits<unsigned int>::max());
    char *memory = static_cast<char *>(std::malloc(length));
    *buffer = uv_buf_init(memory, memory ? static_cast<unsigned int>(length) : 0);
}

void TcpConnection::onRead(uv_stream_t *stream,
                           ssize_t bytesRead,
                           const uv_buf_t *buffer) noexcept
{
    auto *connection = static_cast<TcpConnection *>(stream->data);
    if (bytesRead > 0)
    {
        Ptr self = connection->shared_from_this();
        if (connection->callbacks_.onData)
        {
            try
            {
                connection->callbacks_.onData(self,
                                               std::string_view(buffer->base,
                                                                static_cast<std::size_t>(bytesRead)));
            }
            catch (...)
            {
                connection->loop_.reportException(std::current_exception());
                connection->closeInLoop(UV_EFAULT);
            }
        }
    }
    std::free(buffer->base);
    if (bytesRead == UV_EOF)
    {
        if (!connection->peerReadEof_)
        {
            connection->peerReadEof_ = true;
            if (connection->reading_)
            {
                uv_read_stop(stream);
                connection->reading_ = false;
            }
            if (connection->callbacks_.onPeerHalfClose)
            {
                try
                {
                    connection->callbacks_.onPeerHalfClose(connection->shared_from_this());
                }
                catch (...)
                {
                    connection->loop_.reportException(std::current_exception());
                    connection->closeInLoop(UV_EFAULT);
                }
            }
        }
        if (connection->shutdownCompleted_)
        {
            connection->closeInLoop();
        }
        else if (connection->shutdownRequested_.load(std::memory_order_acquire) &&
                 connection->pendingWriteBytes() == 0)
        {
            connection->beginShutdownInLoop();
        }
    }
    else if (bytesRead < 0)
    {
        connection->closeInLoop(static_cast<int>(bytesRead));
    }
}

void TcpConnection::onWrite(uv_write_t *request, int status) noexcept
{
    std::unique_ptr<WriteRequest> writeRequest(static_cast<WriteRequest *>(request->data));
    TcpConnection::Ptr connection = writeRequest->connection;
    const std::size_t payloadSize = writeRequest->payload.size();
    const std::size_t previous = connection->pendingWriteBytes_.fetch_sub(payloadSize,
                                                                           std::memory_order_acq_rel);
    if (writeRequest->callback)
    {
        try
        {
            writeRequest->callback(connection, Error::fromUv(status));
        }
        catch (...)
        {
            connection->loop_.reportException(std::current_exception());
            connection->closeInLoop(UV_EFAULT);
        }
    }
    if (status < 0)
    {
        connection->notifyError(status);
        connection->closeInLoop();
    }
    else if (previous > connection->options_.writeLowWatermarkBytes &&
             previous - payloadSize <= connection->options_.writeLowWatermarkBytes &&
             (connection->callbacks_.onWriteDrained || connection->writableCallback_))
    {
        try
        {
            if (connection->callbacks_.onWriteDrained)
                connection->callbacks_.onWriteDrained(connection);
            if (connection->writableCallback_)
                connection->writableCallback_();
        }
        catch (...)
        {
            connection->loop_.reportException(std::current_exception());
            connection->closeInLoop(UV_EFAULT);
        }
    }
    if (connection->shutdownRequested_.load(std::memory_order_acquire) &&
        connection->pendingWriteBytes() == 0)
    {
        connection->beginShutdownInLoop();
    }
}

void TcpConnection::onShutdown(uv_shutdown_t *request, int status) noexcept
{
    auto *connection = static_cast<TcpConnection *>(request->data);
    if (status < 0)
    {
        connection->notifyError(status);
        connection->closeInLoop();
        return;
    }
    connection->shutdownCompleted_ = true;
    if (connection->peerReadEof_)
    {
        connection->closeInLoop();
    }
}

void TcpConnection::onClosed(uv_handle_t *handle) noexcept
{
    auto *connection = static_cast<TcpConnection *>(handle->data);
    Ptr self = connection->keepAlive_;
    connection->state_.store(State::Closed, std::memory_order_release);
    connection->handleClosed_ = true;
    if (connection->callbacks_.onClosed)
    {
        try
        {
            connection->callbacks_.onClosed(self);
        }
        catch (...)
        {
            connection->loop_.reportException(std::current_exception());
        }
    }
    connection->keepAlive_.reset();
}

bool TcpConnection::initializeAccepted(uv_stream_t *server) noexcept
{
    int status = uv_tcp_init(&loop_.loop_, &handle_);
    if (status < 0)
    {
        return false;
    }
    handleInitialized_ = true;
    handle_.data = this;
    keepAlive_ = shared_from_this();
    status = uv_accept(server, reinterpret_cast<uv_stream_t *>(&handle_));
    if (status < 0)
    {
        closeInLoop(status);
        return false;
    }
    state_.store(State::Connected, std::memory_order_release);
    startReadInLoop();
    return state() == State::Connected;
}

void TcpConnection::beginConnectInLoop(std::string host,
                                      std::uint16_t port,
                                      ConnectCallback callback) noexcept
{
    if (state() != State::Connecting)
    {
        return;
    }
    connectHost_ = std::move(host);
    connectService_ = std::to_string(port);
    connectCallback_ = std::move(callback);
    int status = uv_tcp_init(&loop_.loop_, &handle_);
    if (status < 0)
    {
        finishConnect(Error::fromUv(status));
        state_.store(State::Closed, std::memory_order_release);
        return;
    }
    handleInitialized_ = true;
    handle_.data = this;
    keepAlive_ = shared_from_this();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    resolverRequest_.data = this;
    resolverKeepAlive_ = shared_from_this();
    resolverPending_ = true;
    status = uv_getaddrinfo(&loop_.loop_,
                            &resolverRequest_,
                            &TcpConnection::onResolved,
                            connectHost_.c_str(),
                            connectService_.c_str(),
                            &hints);
    if (status < 0)
    {
        resolverPending_ = false;
        resolverKeepAlive_.reset();
        finishConnect(Error::fromUv(status));
        closeInLoop();
    }
}

void TcpConnection::connectNextAddressInLoop() noexcept
{
    while (nextConnectAddress_ < connectAddresses_.size())
    {
        const sockaddr_storage &address = connectAddresses_[nextConnectAddress_++];
        connectRequest_.data = this;
        const int status = uv_tcp_connect(&connectRequest_,
                                          &handle_,
                                          reinterpret_cast<const sockaddr *>(&address),
                                          &TcpConnection::onConnect);
        if (status == 0)
        {
            return;
        }
        lastConnectError_ = status;
    }
    connectKeepAlive_.reset();
    finishConnect(Error::fromUv(lastConnectError_));
    closeInLoop();
}
void TcpConnection::finishConnect(Error error) noexcept
{
    if (!connectCallback_)
    {
        return;
    }
    ConnectCallback callback = std::move(connectCallback_);
    try
    {
        callback(shared_from_this(), error);
    }
    catch (...)
    {
        loop_.reportException(std::current_exception());
        closeInLoop(UV_EFAULT);
    }
}

void TcpConnection::onResolved(uv_getaddrinfo_t *request,
                               int status,
                               addrinfo *addresses) noexcept
{
    auto *connection = static_cast<TcpConnection *>(request->data);
    Ptr self = connection->resolverKeepAlive_;
    connection->resolverPending_ = false;
    connection->resolverKeepAlive_.reset();
    if (status < 0)
    {
        connection->finishConnect(Error::fromUv(status));
        connection->closeInLoop();
        return;
    }
    if (!addresses)
    {
        connection->finishConnect(Error::fromUv(UV_EAI_NONAME));
        connection->closeInLoop();
        return;
    }
    if (uv_is_closing(reinterpret_cast<uv_handle_t *>(&connection->handle_)))
    {
        uv_freeaddrinfo(addresses);
        return;
    }

    try
    {
        for (addrinfo *address = addresses; address; address = address->ai_next)
        {
            if ((address->ai_family == AF_INET || address->ai_family == AF_INET6) &&
                address->ai_addrlen <= sizeof(sockaddr_storage))
            {
                sockaddr_storage storage{};
                std::memcpy(&storage, address->ai_addr, address->ai_addrlen);
                connection->connectAddresses_.push_back(storage);
            }
        }
    }
    catch (...)
    {
        uv_freeaddrinfo(addresses);
        connection->finishConnect(Error::fromUv(UV_ENOMEM));
        connection->closeInLoop();
        return;
    }
    uv_freeaddrinfo(addresses);
    std::stable_sort(connection->connectAddresses_.begin(),
                     connection->connectAddresses_.end(),
                     [](const sockaddr_storage &left, const sockaddr_storage &right) {
                         return left.ss_family == AF_INET && right.ss_family != AF_INET;
                     });
    if (connection->connectAddresses_.empty())
    {
        connection->finishConnect(Error::fromUv(UV_EAI_NONAME));
        connection->closeInLoop();
        return;
    }
    connection->connectKeepAlive_ = self;
    connection->connectNextAddressInLoop();
}

void TcpConnection::onConnect(uv_connect_t *request, int status) noexcept
{
    auto *connection = static_cast<TcpConnection *>(request->data);
    Ptr self = connection->connectKeepAlive_;
    if (status < 0)
    {
        connection->lastConnectError_ = status;
        connection->connectNextAddressInLoop();
        return;
    }
    connection->connectKeepAlive_.reset();
    if (connection->state() != State::Connecting)
    {
        connection->closeInLoop();
        return;
    }
    connection->state_.store(State::Connected, std::memory_order_release);
    connection->startReadInLoop();
    if (connection->state() == State::Connected)
    {
        connection->finishConnect(Error{});
    }
}
void TcpConnection::startReadInLoop() noexcept
{
    if (state() != State::Connected || reading_ || peerReadEof_)
    {
        return;
    }
    int status = uv_read_start(reinterpret_cast<uv_stream_t *>(&handle_),
                               &TcpConnection::allocateReadBuffer,
                               &TcpConnection::onRead);
    if (status < 0)
    {
        notifyError(status);
        closeInLoop();
        return;
    }
    reading_ = true;
    invoke(callbacks_.onConnected);
}

void TcpConnection::pauseReadInLoop() noexcept
{
    if (!reading_ || state() == State::Closed)
    {
        return;
    }
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&handle_));
    reading_ = false;
}

void TcpConnection::resumeReadInLoop() noexcept
{
    startReadInLoop();
}

void TcpConnection::enqueueWriteInLoop(std::string data, WriteCallback callback) noexcept
{
    if (state() != State::Connected && state() != State::Disconnecting)
    {
        pendingWriteBytes_.fetch_sub(data.size(), std::memory_order_acq_rel);
        if (callback)
        {
            try
            {
                callback(shared_from_this(), Error::fromUv(UV_ENOTCONN));
            }
            catch (...)
            {
                loop_.reportException(std::current_exception());
            }
        }
        return;
    }
    auto request = std::make_unique<WriteRequest>();
    request->connection = shared_from_this();
    request->payload = std::move(data);
    request->callback = std::move(callback);
    request->request.data = request.get();
    uv_buf_t buffer = uv_buf_init(request->payload.data(),
                                  static_cast<unsigned int>(request->payload.size()));
    int status = uv_write(&request->request,
                          reinterpret_cast<uv_stream_t *>(&handle_),
                          &buffer,
                          1,
                          &TcpConnection::onWrite);
    if (status < 0)
    {
        pendingWriteBytes_.fetch_sub(request->payload.size(), std::memory_order_acq_rel);
        if (request->callback)
        {
            try
            {
                request->callback(request->connection, Error::fromUv(status));
            }
            catch (...)
            {
                loop_.reportException(std::current_exception());
            }
        }
        notifyError(status);
        closeInLoop();
        return;
    }
    request.release();
}

void TcpConnection::beginShutdownInLoop() noexcept
{
    if (shutdownStarted_ || state() == State::Closed || pendingWriteBytes() != 0)
    {
        return;
    }
    shutdownStarted_ = true;
    shutdownRequest_.data = this;
    int status = uv_shutdown(&shutdownRequest_,
                             reinterpret_cast<uv_stream_t *>(&handle_),
                             &TcpConnection::onShutdown);
    if (status < 0)
    {
        notifyError(status);
        closeInLoop();
    }
}

void TcpConnection::closeInLoop(int errorCode) noexcept
{
    if (state() == State::Closed || !handleInitialized_ ||
        uv_is_closing(reinterpret_cast<uv_handle_t *>(&handle_)))
    {
        return;
    }
    state_.store(State::Disconnecting, std::memory_order_release);
    if (reading_)
    {
        uv_read_stop(reinterpret_cast<uv_stream_t *>(&handle_));
        reading_ = false;
    }
    if (resolverPending_)
    {
        uv_cancel(reinterpret_cast<uv_req_t *>(&resolverRequest_));
    }
    if (errorCode < 0)
    {
        notifyError(errorCode);
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&handle_), &TcpConnection::onClosed);
}

void TcpConnection::notifyError(int errorCode) noexcept
{
    if (!callbacks_.onError)
    {
        return;
    }
    try
    {
        callbacks_.onError(shared_from_this(), Error::fromUv(errorCode));
    }
    catch (...)
    {
        loop_.reportException(std::current_exception());
    }
}

void TcpConnection::invoke(const std::function<void(const Ptr &)> &callback) noexcept
{
    if (!callback)
    {
        return;
    }
    try
    {
        callback(shared_from_this());
    }
    catch (...)
    {
        loop_.reportException(std::current_exception());
        closeInLoop(UV_EFAULT);
    }
}

}  // namespace uvhttp
