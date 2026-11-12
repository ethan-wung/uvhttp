/**
 * @file        tls_stream.cc
 * @brief       基于 OpenSSL 的异步 TLS 字节流及 ALPN 协商的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/tls_stream.h"

#include <algorithm>
#include <array>
#include <climits>
#include <deque>
#include <mutex>
#include <utility>
#include <openssl/err.h>

namespace uvhttp
{

struct TlsStream::Impl
{
    Impl(TcpConnection::Ptr stream, SSL_CTX *ctx, bool isServer,
         Callbacks value, std::vector<std::string> protocols, std::string host, bool verify)
        : connection(std::move(stream)), context(ctx), server(isServer),
          callbacks(std::move(value)), alpn(std::move(protocols)),
          peerHost(std::move(host)), verifyPeer(verify) {}

    TcpConnection::Ptr connection;
    SSL_CTX *context{nullptr};
    SSL *ssl{nullptr};
    bool server{false};
    bool complete{false};
    bool closed{false};
    Callbacks callbacks;
    std::vector<std::string> alpn;
    std::string negotiated;
    std::string peerHost;
    bool verifyPeer{true};
    std::deque<std::string> encryptedQueue;
    std::size_t encryptedQueueBytes{0};
    bool encryptedBlocked{false};
    std::size_t maxEncryptedQueueBytes{16 * 1024 * 1024};
    WritableCallback writableCallback;
};

TlsStream::TlsStream(TcpConnection::Ptr connection,
                     SSL_CTX *context, bool server, Callbacks callbacks,
                     std::vector<std::string> alpnProtocols, std::string peerHost, bool verifyPeer)
    : impl_(std::make_unique<Impl>(std::move(connection), context, server,
                                   std::move(callbacks), std::move(alpnProtocols),
                                   std::move(peerHost), verifyPeer))
{
}

TlsStream::~TlsStream()
{
    if (impl_ && impl_->ssl) SSL_free(impl_->ssl);
}

bool TlsStream::initialize() noexcept
{
    if (!impl_->connection || !impl_->context) return false;
    impl_->ssl = SSL_new(impl_->context);
    if (!impl_->ssl) return false;
    BIO *input = BIO_new(BIO_s_mem());
    BIO *output = BIO_new(BIO_s_mem());
    if (!input || !output)
    {
        BIO_free(input);
        BIO_free(output);
        return false;
    }
    BIO_set_mem_eof_return(input, -1);
    BIO_set_mem_eof_return(output, -1);
    SSL_set_bio(impl_->ssl, input, output);
    SSL_set_app_data(impl_->ssl, this);
    if (impl_->server)
    {
        SSL_set_accept_state(impl_->ssl);
        SSL_CTX_set_alpn_select_cb(impl_->context, &TlsStream::selectAlpn, nullptr);
    }
    else
    {
        SSL_set_connect_state(impl_->ssl);
        if (impl_->verifyPeer)
        {
            // An explicit trust store may already be installed by the caller.
            // Default CA paths are optional on Windows/MinGW and must not make
            // an otherwise valid SSL_CTX fail initialization.
            (void)SSL_CTX_set_default_verify_paths(impl_->context);
            if (SSL_set1_host(impl_->ssl, impl_->peerHost.c_str()) != 1)
                return false;
        }
        SSL_set_verify(impl_->ssl, impl_->verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
        if (!impl_->alpn.empty())
        {
            std::vector<unsigned char> wire;
            for (const auto &protocol : impl_->alpn)
            {
                if (protocol.empty() || protocol.size() > 255) return false;
                wire.push_back(static_cast<unsigned char>(protocol.size()));
                wire.insert(wire.end(), protocol.begin(), protocol.end());
            }
            if (SSL_set_alpn_protos(impl_->ssl, wire.data(), static_cast<unsigned int>(wire.size())) != 0)
                return false;
        }
    }
    driveHandshake();
    return !impl_->closed;
}

TlsStream::Ptr TlsStream::accept(TcpConnection::Ptr connection,
                                 SSL_CTX *context,
                                 Callbacks callbacks,
                                 std::vector<std::string> alpnProtocols)
{
    auto stream = Ptr(new TlsStream(std::move(connection), context, true,
                                    std::move(callbacks), std::move(alpnProtocols)));
    if (!stream->initialize())
    {
        stream->fail("failed to initialize TLS server stream", UV_EINVAL);
        return {};
    }
    return stream;
}

bool TlsStream::connect(EventLoop &loop, TcpClient &client, std::string host,
                        std::uint16_t port, SSL_CTX *context, Callbacks callbacks,
                        std::vector<std::string> alpnProtocols, bool verifyPeer,
                        TcpConnection::Ptr *pendingConnection)
{
    if (&client.eventLoop() != &loop) return false;
    struct Pending
    {
        std::shared_ptr<SSL_CTX> context;
        Callbacks callbacks;
        std::vector<std::string> protocols;
        std::string serverName;
        bool verifyPeer;
        Ptr stream;
    };
    if (!context || SSL_CTX_up_ref(context) != 1) return false;
    auto ownedContext = std::shared_ptr<SSL_CTX>(context, SSL_CTX_free);
    auto pending = std::make_shared<Pending>(Pending{std::move(ownedContext), std::move(callbacks),
                                                      std::move(alpnProtocols), host, verifyPeer, {}});
    TcpConnection::Callbacks tcpCallbacks;
    tcpCallbacks.onData = [pending](const TcpConnection::Ptr &, std::string_view bytes) {
        if (pending->stream) pending->stream->processEncrypted(bytes);
    };
    tcpCallbacks.onPeerHalfClose = [pending](const TcpConnection::Ptr &) {
        if (pending->stream) pending->stream->peerClosed();
    };
    tcpCallbacks.onError = [pending](const TcpConnection::Ptr &, const Error &error) {
        if (pending->stream && pending->stream->impl_->callbacks.onError)
            pending->stream->impl_->callbacks.onError(error);
    };
    tcpCallbacks.onClosed = [pending](const TcpConnection::Ptr &) {
        if (pending->stream && pending->stream->impl_->callbacks.onClosed)
            pending->stream->impl_->callbacks.onClosed();
        pending->stream.reset();
    };
    return client.connect(std::move(host), port, std::move(tcpCallbacks),
        [pending](const TcpConnection::Ptr &connection, const Error &error) {
            if (error)
            {
                if (pending->callbacks.onError) pending->callbacks.onError(error);
                return;
            }
            pending->stream = Ptr(new TlsStream(connection, pending->context.get(), false,
                std::move(pending->callbacks), std::move(pending->protocols),
                std::move(pending->serverName), pending->verifyPeer));
            if (!pending->stream->initialize())
                pending->stream->fail("failed to initialize TLS client stream", UV_EINVAL);
        }, pendingConnection);
}


bool TlsStream::isLoopThread() const noexcept
{
    return impl_ && impl_->connection && impl_->connection->isLoopThread();
}

bool TlsStream::post(Task task) noexcept
{
    return impl_ && impl_->connection && impl_->connection->eventLoop().post(std::move(task));
}


AsyncByteStream::WriteStatus TlsStream::writeBytes(std::string data) noexcept
{
    if (!write(std::move(data))) return WriteStatus::Backpressured;
    return WriteStatus::Accepted;
}

bool TlsStream::shutdownWrite() noexcept
{
    if (!impl_ || impl_->closed) return false;
    auto self = shared_from_this();
    return impl_->connection->eventLoop().post([self] {
        if (!self->impl_ || self->impl_->closed) return;
        SSL_shutdown(self->impl_->ssl);
        self->drainEncrypted();
        if (self->impl_ && !self->impl_->closed) self->impl_->connection->shutdown();
    });
}

bool TlsStream::setWritableCallback(WritableCallback callback) noexcept
{
    if (!impl_ || impl_->closed) return false;
    auto weak = weak_from_this();
    auto install = [weak, callback = std::move(callback)]() mutable {
        auto self = weak.lock();
        if (!self || !self->impl_) return;
        self->impl_->writableCallback = std::move(callback);
        self->impl_->connection->setWritableCallback([weak] {
            if (auto stream = weak.lock()) stream->flushEncrypted();
        });
    };
    if (impl_->connection->isLoopThread()) { install(); return true; }
    return impl_->connection->eventLoop().post(std::move(install));
}
bool TlsStream::write(std::string data) noexcept
{
    if (!impl_ || impl_->closed) return false;
    if (!impl_->connection->isLoopThread())
    {
        auto self = shared_from_this();
        return impl_->connection->eventLoop().post([self, data = std::move(data)]() mutable {
            self->writeInLoop(std::move(data));
        });
    }
    return writeInLoop(std::move(data));
}

bool TlsStream::writeInLoop(std::string data) noexcept
{
    if (!impl_ || impl_->closed || !impl_->complete) return false;
    const auto available = impl_->maxEncryptedQueueBytes -
                           std::min(impl_->encryptedQueueBytes, impl_->maxEncryptedQueueBytes);
    const auto records = data.size() / (16 * 1024) + (data.size() % (16 * 1024) != 0);
    const auto overhead = records * 64;
    if (data.size() > available || overhead > available - data.size()) return false;
    std::size_t written = 0;
    while (written < data.size())
    {
        const int result = SSL_write(impl_->ssl, data.data() + written,
                                     static_cast<int>(std::min<std::size_t>(data.size() - written, INT_MAX)));
        if (result <= 0)
        {
            fail("TLS plaintext write failed", UV_EPROTO);
            return false;
        }
        written += static_cast<std::size_t>(result);
        drainEncrypted();
        if (impl_->closed) return false;
    }
    return true;
}

void TlsStream::processEncrypted(std::string_view bytes) noexcept
{
    if (!impl_ || impl_->closed) return;
    BIO *input = SSL_get_rbio(impl_->ssl);
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const int count = static_cast<int>(std::min<std::size_t>(bytes.size() - offset, INT_MAX));
        const int result = BIO_write(input, bytes.data() + offset, count);
        if (result <= 0) { fail("TLS encrypted input rejected", UV_EPROTO); return; }
        offset += static_cast<std::size_t>(result);
    }
    if (!impl_->complete) driveHandshake();
    if (impl_->complete) readPlaintext();
}

void TlsStream::driveHandshake() noexcept
{
    const int result = SSL_do_handshake(impl_->ssl);
    drainEncrypted();
    if (result == 1)
    {
        impl_->complete = true;
        const unsigned char *protocol = nullptr;
        unsigned int length = 0;
        SSL_get0_alpn_selected(impl_->ssl, &protocol, &length);
        impl_->negotiated.assign(reinterpret_cast<const char *>(protocol), length);
        if (impl_->callbacks.onHandshake) impl_->callbacks.onHandshake(shared_from_this());
        readPlaintext();
        return;
    }
    const int error = SSL_get_error(impl_->ssl, result);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
    {
        char errorText[256]{};
        const unsigned long errorCode = ERR_get_error();
        if (errorCode != 0)
            ERR_error_string_n(errorCode, errorText, sizeof(errorText));
        fail(errorCode != 0 ? std::string("TLS handshake failed: ") + errorText : std::string("TLS handshake failed"), UV_EPROTO);
    }

}

void TlsStream::drainEncrypted() noexcept
{
    if (!impl_ || !impl_->ssl || impl_->closed) return;
    BIO *output = SSL_get_wbio(impl_->ssl);
    std::array<char, 16 * 1024> buffer{};
    while (BIO_ctrl_pending(output) > 0)
    {
        const int count = BIO_read(output, buffer.data(), static_cast<int>(buffer.size()));
        if (count <= 0) break;
        std::string encrypted(buffer.data(), static_cast<std::size_t>(count));
        if (encrypted.size() > impl_->maxEncryptedQueueBytes -
                                    std::min(impl_->encryptedQueueBytes, impl_->maxEncryptedQueueBytes))
        {
            fail("TLS encrypted output queue limit exceeded", UV_ENOBUFS);
            return;
        }
        impl_->encryptedQueueBytes += encrypted.size();
        impl_->encryptedQueue.emplace_back(std::move(encrypted));
    }
    flushEncrypted();
}

void TlsStream::flushEncrypted() noexcept
{
    if (!impl_ || impl_->closed) return;
    while (!impl_->encryptedQueue.empty())
    {
        auto result = impl_->connection->write(impl_->encryptedQueue.front());
        if (result == TcpConnection::WriteResult::Backpressured) { impl_->encryptedBlocked = true; return; }
        if (result != TcpConnection::WriteResult::Queued)
        {
            fail("TLS transport closed while flushing encrypted output", UV_EPIPE);
            return;
        }
        impl_->encryptedQueueBytes -= impl_->encryptedQueue.front().size();
        impl_->encryptedQueue.pop_front();
    }
    if (impl_->encryptedBlocked)
    {
        impl_->encryptedBlocked = false;
        if (impl_->writableCallback)
        {
            try { impl_->writableCallback(); }
            catch (...) { fail("TLS writable callback threw an exception", UV_EFAULT); }
        }
    }}
void TlsStream::readPlaintext() noexcept
{
    std::array<char, 16 * 1024> buffer{};
    for (;;)
    {
        const int result = SSL_read(impl_->ssl, buffer.data(), static_cast<int>(buffer.size()));
        if (result > 0)
        {
            if (impl_->callbacks.onData)
                impl_->callbacks.onData(shared_from_this(), std::string_view(buffer.data(), static_cast<std::size_t>(result)));
            continue;
        }
        const int error = SSL_get_error(impl_->ssl, result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) break;
        if (error == SSL_ERROR_ZERO_RETURN) { peerClosed(); break; }
        if (error != SSL_ERROR_SYSCALL || result < 0) fail("TLS plaintext read failed", UV_EPROTO);
        break;
    }
    drainEncrypted();
}

void TlsStream::peerClosed() noexcept
{
    if (!impl_ || impl_->closed) return;
    if (impl_->complete && impl_->callbacks.onError)
        impl_->callbacks.onError(Error{UV_EOF, "TLS peer closed connection"});
}

void TlsStream::fail(std::string message, int code) noexcept
{
    if (!impl_ || impl_->closed) return;
    impl_->closed = true;
    if (impl_->callbacks.onError) impl_->callbacks.onError(Error{code, std::move(message)});
    if (impl_->connection) impl_->connection->close();
}

void TlsStream::close() noexcept
{
    if (!impl_ || !impl_->connection) return;
    auto self = shared_from_this();
    auto closeInLoop = [self] {
        if (!self->impl_ || self->impl_->closed) return;
        self->impl_->closed = true;
        if (self->impl_->connection) self->impl_->connection->close();
    };
    if (impl_->connection->isLoopThread()) closeInLoop();
    else if (!impl_->connection->eventLoop().post(std::move(closeInLoop))) impl_->connection->close();
}

bool TlsStream::handshakeComplete() const noexcept { return impl_ && impl_->complete; }
std::string TlsStream::negotiatedAlpn() const { return impl_ ? impl_->negotiated : std::string{}; }
TcpConnection::Ptr TlsStream::tcpConnection() const { return impl_ ? impl_->connection : TcpConnection::Ptr{}; }

bool TlsStream::feedEncrypted(std::string_view bytes) noexcept
{
    if (!impl_ || !impl_->server) return false;
    processEncrypted(bytes);
    return !impl_->closed;
}

int TlsStream::selectAlpn(SSL *ssl, const unsigned char **out, unsigned char *outLength,
                          const unsigned char *input, unsigned int inputLength, void *) noexcept
{
    auto *stream = static_cast<TlsStream *>(SSL_get_app_data(ssl));
    if (!stream || stream->impl_->alpn.empty()) return SSL_TLSEXT_ERR_NOACK;
    for (const auto &protocol : stream->impl_->alpn)
    {
        const unsigned char *cursor = input;
        const unsigned char *end = input + inputLength;
        while (cursor < end)
        {
            const unsigned int length = *cursor++;
            if (cursor + length > end) return SSL_TLSEXT_ERR_ALERT_FATAL;
            if (length == protocol.size() && std::equal(protocol.begin(), protocol.end(), cursor))
            {
                *out = cursor;
                *outLength = static_cast<unsigned char>(length);
                return SSL_TLSEXT_ERR_OK;
            }
            cursor += length;
        }
    }
    return SSL_TLSEXT_ERR_NOACK;
}

}  // namespace uvhttp
