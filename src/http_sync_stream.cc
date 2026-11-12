/**
 * @file http_sync_stream.cc
 * @brief 有界接收队列、读背压和取消唤醒实现。
 * @author Ethan
 * @version 1.0 初次建立
 * @date 2026.11.11
 */
#include "uvhttp/http_sync_stream.h"
#include "uvhttp/event_loop_thread.h"
#include "client_pool.h"
#include <condition_variable>
#include <future>
#include <cstring>
#include <stdexcept>
namespace uvhttp {
struct HttpSyncStream::Impl {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::string> chunks;
        std::size_t bytes{0},offset{0};
        HttpResponseHead head;
        Error error;
        bool ready{false},headReady{false},done{false},ended{false},paused{false};
        std::function<void(bool)> readControl;
        HttpRequestBodyWriter writer;
#ifdef UVHTTP_WITH_HTTP2
        Http2Session::Ptr h2;
        std::int32_t id{0};
#endif
    };
    Options options;
    EventLoopThread thread;
    EventLoop *loop;
    std::shared_ptr<detail::ClientPool> pool;
    mutable std::mutex currentMutex;
    std::shared_ptr<State> current;
    std::atomic<bool> canceled{false};
    explicit Impl(Options value) : options(std::move(value)),loop(nullptr) {
        if(options.timeout.count()<=0 || options.highWaterBytes<65536 || options.highWaterBytes>64*1024*1024)
            throw std::invalid_argument("invalid synchronous stream limits");
#ifndef UVHTTP_WITH_HTTP2
        if(options.protocol==Protocol::Http2) throw std::invalid_argument("HTTP/2 was not built");
#endif
        options.pool.requestTimeout=options.timeout;
        options.pool.maxOutstandingRequests=1;
        options.pool.maxConnectionsPerOrigin=1;
        loop=&thread.start();
        pool=std::make_shared<detail::ClientPool>(*loop,TcpConnection::Options{},options.pool,
                                                options.protocol==Protocol::Http2,1,65536,65536);
    }
    ~Impl() { cancel(); pool.reset(); thread.stop(); thread.join(); }
    std::shared_ptr<State> state() const { std::lock_guard<std::mutex> lock(currentMutex); return current; }
    static void finish(const std::shared_ptr<State> &state,Error error) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->done) return;
        state->error=std::move(error); state->done=true; state->changed.notify_all();
    }
    void cancel() noexcept {
        canceled.store(true);
        auto value=state(); if(value) finish(value,{UV_ECANCELED,"synchronous stream canceled"});
        if(pool) pool->close();
    }
    bool begin(std::string host,std::uint16_t port,SSL_CTX *context,HttpRequestHead request,Error &error,bool verify) {
        error={};
        if(canceled.load()) { error={UV_ECANCELED,"stream session is closed"}; return false; }
        if(options.protocol==Protocol::Http1 && request.version!="HTTP/1.1") { error={UV_EINVAL,"streamed HTTP/1 upload requires HTTP/1.1"}; return false; }
        auto value=std::make_shared<State>();
        {
            std::lock_guard<std::mutex> guard(currentMutex);
            if(current) {
                std::lock_guard<std::mutex> lock(current->mutex);
                if(!current->done || current->bytes) { error={UV_EBUSY,"previous response has not been consumed"}; return false; }
            }
            current=value;
        }
        auto job=std::make_shared<detail::ClientPool::Job>();
        static_cast<HttpRequestHead &>(job->request)=std::move(request);
        job->streaming=true; job->upload=true;
        job->readControlReady=[value](std::function<void(bool)> control) {
            std::lock_guard<std::mutex> lock(value->mutex); value->readControl=std::move(control);
        };
        job->uploadReady=[value](HttpRequestBodyWriter writer) {
            std::lock_guard<std::mutex> lock(value->mutex); value->writer=std::move(writer); value->ready=true; value->changed.notify_all();
        };
#ifdef UVHTTP_WITH_HTTP2
        job->h2Ready=[value](const Http2Session::Ptr &session,std::int32_t id) {
            std::lock_guard<std::mutex> lock(value->mutex); value->h2=session; value->id=id; value->ready=true; value->changed.notify_all();
        };
#endif
        job->callbacks.onResponseHead=[value](const HttpResponseHead &head) {
            std::lock_guard<std::mutex> lock(value->mutex); value->head=head; value->headReady=true; value->changed.notify_all();
        };
        std::weak_ptr<detail::ClientPool> weakPool=pool;
        job->callbacks.onResponseBody=[value,limit=options.highWaterBytes,weakPool](std::string_view bytes) {
            std::unique_lock<std::mutex> lock(value->mutex);
            if(value->done || bytes.empty()) return;
            if(bytes.size()>limit+1024*1024-std::min(value->bytes,limit+1024*1024)) {
                value->error={UV_ENOBUFS,"receive buffer hard limit exceeded"}; value->done=true; value->changed.notify_all();
                lock.unlock(); if(auto pool=weakPool.lock()) pool->close(); return;
            }
            value->chunks.emplace_back(bytes); value->bytes+=bytes.size();
            if(value->bytes>=limit && !value->paused && value->readControl) {
                value->paused=true; value->readControl(true);
            }
            value->changed.notify_all();
        };
        job->callbacks.onComplete=[value] { finish(value,{}); };
        job->callbacks.onError=[value](const Error &failure) { finish(value,failure); };
        const std::vector<std::string> alpn=context ? std::vector<std::string>{options.protocol==Protocol::Http2 ? "h2" : "http/1.1"} : std::vector<std::string>{};
        if(!pool->submit(std::move(host),port,context,verify,alpn,job)) finish(value,{UV_ECANCELED,"stream request rejected"});
        std::unique_lock<std::mutex> lock(value->mutex);
        value->changed.wait(lock,[&]{return value->ready || value->done;});
        error=value->error; return value->ready && !error;
    }
    bool write(std::string bytes,bool ending,Error &error) {
        error={}; auto value=state();
        if(!value) { error={UV_EINVAL,"no active request"}; return false; }
        if(bytes.size()>65536) { error={UV_EMSGSIZE,"write chunk exceeds 64 KiB"}; return false; }
        auto promise=std::make_shared<std::promise<Error>>(); auto future=promise->get_future();
        if(!loop->post([value,bytes=std::move(bytes),ending,promise]() mutable {
            HttpRequestBodyWriter writer;
#ifdef UVHTTP_WITH_HTTP2
            Http2Session::Ptr session; std::int32_t id=0;
#endif
            {
                std::lock_guard<std::mutex> lock(value->mutex);
                if(value->error || value->done || value->ended || !value->ready) {
                    promise->set_value(value->error ? value->error : Error{UV_EPIPE,"upload is not writable"}); return;
                }
                writer=value->writer;
#ifdef UVHTTP_WITH_HTTP2
                session=value->h2; id=value->id;
#endif
            }
            bool accepted;
#ifdef UVHTTP_WITH_HTTP2
            if(session) accepted=ending ? session->endRequestData(id) : session->writeRequestData(id,std::move(bytes));
            else
#endif
                accepted=ending ? writer.end() : writer.write(std::move(bytes));
            if(accepted && ending) { std::lock_guard<std::mutex> lock(value->mutex); value->ended=true; }
            promise->set_value(accepted ? Error{} : Error{UV_EAGAIN,"write rejected; retry only while request remains active"});
        })) { error={UV_ECANCELED,"event loop stopped"}; return false; }
        error=future.get(); return !error;
    }
};
HttpSyncStream::HttpSyncStream() : HttpSyncStream(Options{}) {}
HttpSyncStream::HttpSyncStream(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
HttpSyncStream::~HttpSyncStream()=default;
bool HttpSyncStream::begin(std::string host,std::uint16_t port,HttpRequestHead request,Error &error) {
    return impl_->begin(std::move(host),port,nullptr,std::move(request),error,true);
}
bool HttpSyncStream::beginTls(std::string host,std::uint16_t port,SSL_CTX *context,HttpRequestHead request,Error &error,bool verify) {
    if(!context) { error={UV_EINVAL,"TLS context is required"}; return false; }
    return impl_->begin(std::move(host),port,context,std::move(request),error,verify);
}
bool HttpSyncStream::write(std::string bytes,Error &error) { return impl_->write(std::move(bytes),false,error); }
bool HttpSyncStream::endUpload(Error &error) { return impl_->write({},true,error); }
bool HttpSyncStream::readHead(HttpResponseHead &head,Error &error) {
    error={}; auto state=impl_->state(); if(!state) { error={UV_EINVAL,"no active request"}; return false; }
    std::unique_lock<std::mutex> lock(state->mutex); state->changed.wait(lock,[&]{return state->headReady || state->done;});
    error=state->error; if(!state->headReady || error) return false; head=state->head; return true;
}
std::size_t HttpSyncStream::read(char *destination,std::size_t capacity,Error &error) {
    error={}; if(!destination || !capacity) { error={UV_EINVAL,"invalid read buffer"}; return 0; }
    auto state=impl_->state(); if(!state) { error={UV_EINVAL,"no active request"}; return 0; }
    std::unique_lock<std::mutex> lock(state->mutex); state->changed.wait(lock,[&]{return state->bytes || state->done;});
    if(state->error) { error=state->error; return 0; }
    if(!state->bytes) return 0;
    auto &chunk=state->chunks.front(); const auto count=std::min(capacity,chunk.size()-state->offset);
    std::memcpy(destination,chunk.data()+state->offset,count); state->offset+=count; state->bytes-=count;
    if(state->offset==chunk.size()) { state->chunks.pop_front(); state->offset=0; }
    if(state->paused && state->bytes<=impl_->options.highWaterBytes/2 && state->readControl) {
        state->paused=false; state->readControl(false);
    }
    return count;
}
void HttpSyncStream::cancel() noexcept { impl_->cancel(); }
ConnectionPoolStats HttpSyncStream::poolStats() const { return impl_->pool->stats(); }
}
