/**
 * @file        http2_session.cc
 * @brief       基于 nghttp2 的 HTTP/2 会话、流和协议事件接口的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http2_session.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <utility>

#include <nghttp2/nghttp2.h>

namespace uvhttp
{
namespace
{
std::string lowerName(std::string_view name)
{
    std::string result(name);
    for (auto &ch : result)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return result;
}

bool forbiddenHeader(std::string_view name)
{
    return name == "connection" || name == "proxy-connection" || name == "keep-alive" ||
           name == "transfer-encoding" || name == "upgrade";
}
}

struct Http2Session::Impl
{
    struct Stream
    {
        HttpRequest request;
        HttpResponse response;
        std::size_t offset{0};
        bool responseSide{false};
        bool streaming{false};
        bool outboundStreaming{false};
        bool headNotified{false};
        bool outboundEnded{false};
        bool requestNotified{false};
        std::size_t headerBytes{0};
        bool headRequest{false};
        bool responseSubmitted{false};
        bool suppressBody{false};
        bool rejected{false};
        std::deque<std::string> outbound;
        std::size_t outboundOffset{0};
    };

    AsyncByteStream::Ptr transport;
    Callbacks callbacks;
    nghttp2_session *session{nullptr};
    Http2Session *owner{nullptr};
    bool client{false};
    std::uint32_t localMaxConcurrentStreams{100};
    bool failed{false};
    bool processingInput{false};
    std::size_t maxBodyBytes{0};
    std::size_t maxHeaderBytes{0};
    std::map<std::int32_t, std::unique_ptr<Stream>> streams;
    std::deque<std::string> output;
    std::size_t outputBytes{0};
    static constexpr std::size_t maxOutputBytes = 16 * 1024 * 1024;

    static ssize_t readData(nghttp2_session *, int32_t, uint8_t *buffer, size_t length,
                            uint32_t *flags, nghttp2_data_source *source, void *)
    {
        auto *stream = static_cast<Stream *>(source->ptr);
        if (stream->outboundStreaming)
        {
            if (stream->outbound.empty()) {
                if (stream->outboundEnded) { *flags |= NGHTTP2_DATA_FLAG_EOF; return 0; }
                return NGHTTP2_ERR_DEFERRED;
            }
            auto &chunk = stream->outbound.front();
            const auto available = chunk.size() - std::min(stream->outboundOffset, chunk.size());
            const auto count = std::min(length, available);
            if (count) std::copy_n(chunk.data() + stream->outboundOffset, count, buffer);
            stream->outboundOffset += count;
            if (stream->outboundOffset == chunk.size()) { stream->outbound.pop_front(); stream->outboundOffset = 0; }
            if (stream->outbound.empty() && stream->outboundEnded) *flags |= NGHTTP2_DATA_FLAG_EOF;
            return static_cast<ssize_t>(count);
        }
        const std::string &body = stream->responseSide ? stream->response.body : stream->request.body;
        const auto available = body.size() - std::min(stream->offset, body.size());
        const auto count = std::min(length, available);
        if (count) std::copy_n(body.data() + stream->offset, count, buffer);
        stream->offset += count;
        if (stream->offset == body.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
        return static_cast<ssize_t>(count);
    }
    static int beginHeaders(nghttp2_session *, const nghttp2_frame *frame, void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        if (frame->hd.type != NGHTTP2_HEADERS) return 0;
        auto &state = self->streams[frame->hd.stream_id];
        if (!state) state = std::make_unique<Stream>();
        return 0;
    }

    static int header(nghttp2_session *, const nghttp2_frame *frame,
                      const uint8_t *name, size_t nameLength,
                      const uint8_t *value, size_t valueLength, uint8_t, void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        if (frame->hd.type != NGHTTP2_HEADERS) return 0;
        auto found = self->streams.find(frame->hd.stream_id);
        if (found == self->streams.end()) return 0;
        const std::string_view key(reinterpret_cast<const char *>(name), nameLength);
        const std::string_view field(reinterpret_cast<const char *>(value), valueLength);
        auto &state = *found->second;
        if (nameLength > self->maxHeaderBytes - state.headerBytes) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        state.headerBytes += nameLength;
        if (valueLength > self->maxHeaderBytes - state.headerBytes) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        state.headerBytes += valueLength;
        if (self->client)
        {
            if (key == ":status") state.response.status = std::atoi(std::string(field).c_str());
            else if (!key.empty() && key.front() != ':')
                state.response.headers.add(lowerName(key), std::string(field));
        }
        else
        {
            if (key == ":method") { state.request.method.assign(field); state.headRequest = field == "HEAD"; }
            else if (key == ":path") state.request.target.assign(field);
            else if (key == ":authority") state.request.headers.set("host", std::string(field));
            else if (key == ":scheme") state.request.headers.set(":scheme", std::string(field));
            else if (!key.empty() && key.front() != ':')
                state.request.headers.add(lowerName(key), std::string(field));
        }
        return 0;
    }

    static int dataChunk(nghttp2_session *, uint8_t, int32_t streamId,
                         const uint8_t *data, size_t length, void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        auto found = self->streams.find(streamId);
        if (found == self->streams.end()) return 0;
        if (found->second->rejected) return 0;
        if (found->second->streaming)
        {
            const std::string_view chunk(reinterpret_cast<const char *>(data), length);
            if (self->client && self->callbacks.onResponseBody) self->callbacks.onResponseBody(streamId, chunk);
            if (!self->client && self->callbacks.onRequestBodyWithSession) self->callbacks.onRequestBodyWithSession(self->owner->shared_from_this(),streamId,chunk);
            else if (!self->client && self->callbacks.onRequestBody) self->callbacks.onRequestBody(streamId, chunk);
            return 0;
        }
        auto &body = self->client ? found->second->response.body : found->second->request.body;
        if (length > self->maxBodyBytes || body.size() > self->maxBodyBytes - length)
        {
            found->second->rejected = true;
            const auto result = nghttp2_submit_rst_stream(self->session, NGHTTP2_FLAG_NONE, streamId, NGHTTP2_ENHANCE_YOUR_CALM);
            return result == 0 ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        body.append(reinterpret_cast<const char *>(data), length);
        return 0;
    }

    static int frameReceived(nghttp2_session *, const nghttp2_frame *frame, void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        if (frame->hd.type == NGHTTP2_GOAWAY)
        {
            if (self->callbacks.onGoAway)
                self->callbacks.onGoAway(frame->goaway.last_stream_id);
            return 0;
        }
        if (frame->hd.type == NGHTTP2_SETTINGS)
        {
            if (!(frame->hd.flags & NGHTTP2_FLAG_ACK))
            {
                for (std::size_t index = 0; index < frame->settings.niv; ++index)
                {
                    const auto &entry = frame->settings.iv[index];
                    if (entry.settings_id == NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS &&
                        self->callbacks.onRemoteMaxConcurrentStreams)
                        self->callbacks.onRemoteMaxConcurrentStreams(entry.value);
                }
                if (self->callbacks.onRemoteSettings)
                    self->callbacks.onRemoteSettings();
            }
            return 0;
        }
        if (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA) return 0;
        auto found = self->streams.find(frame->hd.stream_id);
        if (found == self->streams.end()) return 0;
        auto &state = *found->second;
        if (state.rejected) return 0;
        if (frame->hd.type == NGHTTP2_HEADERS &&
            (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) && !state.headNotified)
        {
            if (self->client && state.response.status >= 100 && state.response.status < 200)
            {
                state.response = HttpResponse{};
                state.headerBytes = 0;
                return 0;
            }
            state.headNotified = true;
            state.streaming = self->client ? static_cast<bool>(self->callbacks.onResponseBody || self->callbacks.onResponseEnd)
                                           : static_cast<bool>(self->callbacks.onRequestBodyWithSession || self->callbacks.onRequestBody || self->callbacks.onRequestEnd);
            if (self->client && self->callbacks.onResponseHead)
                self->callbacks.onResponseHead(frame->hd.stream_id, state.response);
            if (!self->client && self->callbacks.onRequestHead)
                self->callbacks.onRequestHead(self->owner->shared_from_this(), frame->hd.stream_id, state.request);
            if (!self->client && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) &&
                !state.streaming && self->callbacks.onRequest)
            {
                state.requestNotified = true;
                self->callbacks.onRequest(self->owner->shared_from_this(), frame->hd.stream_id, std::move(state.request));
            }
        }
        if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) return 0;
        if (self->client)
        {
            if (found->second->streaming) { if (self->callbacks.onResponseEnd) self->callbacks.onResponseEnd(frame->hd.stream_id); }
            else if (self->callbacks.onResponse) self->callbacks.onResponse(frame->hd.stream_id, std::move(found->second->response));
        }
        else if (found->second->streaming)
        {
            if (self->callbacks.onRequestEnd) self->callbacks.onRequestEnd(self->owner->shared_from_this(), frame->hd.stream_id);
        }
        else if (self->callbacks.onRequest && !found->second->requestNotified)
        {
            found->second->requestNotified = true;
            self->callbacks.onRequest(self->owner->shared_from_this(), frame->hd.stream_id, std::move(found->second->request));
        }
        return 0;
    }

    static int streamClosed(nghttp2_session *, int32_t streamId, uint32_t errorCode, void *userData)
    {
        auto *self = static_cast<Impl *>(userData);
        self->streams.erase(streamId);
        if (self->callbacks.onStreamClosed) self->callbacks.onStreamClosed(streamId, errorCode);
        return 0;
    }

    void report(int code, const char *operation)
    {
        if (failed) return;
        failed = true;
        if (callbacks.onError)
            callbacks.onError(Error{UV_EPROTO, std::string(operation) + ": " + nghttp2_strerror(code)});
    }

    bool sendPending()
    {
        for (;;)
        {
            const uint8_t *data = nullptr;
            const auto length = nghttp2_session_mem_send(session, &data);
            if (length < 0) { report(static_cast<int>(length), "nghttp2_session_mem_send"); return false; }
            if (length == 0) break;
            if (static_cast<std::size_t>(length) > maxOutputBytes - outputBytes)
            {
                report(NGHTTP2_ERR_NOMEM, "HTTP/2 pending output limit exceeded");
                return false;
            }
            output.emplace_back(reinterpret_cast<const char *>(data), static_cast<std::size_t>(length));
            outputBytes += static_cast<std::size_t>(length);
        }
        owner->flushOutput();
        return !failed;
    }
};

Http2Session::Http2Session(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Http2Session::~Http2Session() { if (impl_ && impl_->session) nghttp2_session_del(impl_->session); }

Http2Session::Ptr Http2Session::createClient(AsyncByteStream::Ptr stream, Callbacks callbacks, std::size_t maxHeaderBytes, std::size_t maxBodyBytes)
{
    return create(std::move(stream), std::move(callbacks), true, 100, maxBodyBytes, maxHeaderBytes);
}
Http2Session::Ptr Http2Session::createServer(AsyncByteStream::Ptr stream, Callbacks callbacks, std::uint32_t maxConcurrentStreams, std::size_t maxBodyBytes, std::size_t maxHeaderBytes)
{
    return create(std::move(stream), std::move(callbacks), false, maxConcurrentStreams, maxBodyBytes, maxHeaderBytes);
}

Http2Session::Ptr Http2Session::create(AsyncByteStream::Ptr stream, Callbacks callbacks, bool client, std::uint32_t maxConcurrentStreams, std::size_t maxBodyBytes, std::size_t maxHeaderBytes, bool deferOutput)
{
    if (!stream) return {};
    auto impl = std::make_unique<Impl>();
    impl->transport = std::move(stream);
    impl->callbacks = std::move(callbacks);
    impl->client = client;
    impl->maxBodyBytes = maxBodyBytes;
    impl->maxHeaderBytes = maxHeaderBytes;
    impl->localMaxConcurrentStreams = maxConcurrentStreams;
    nghttp2_session_callbacks *sessionCallbacks = nullptr;
    if (nghttp2_session_callbacks_new(&sessionCallbacks) != 0) return {};
    nghttp2_session_callbacks_set_on_begin_headers_callback(sessionCallbacks, &Impl::beginHeaders);
    nghttp2_session_callbacks_set_on_header_callback(sessionCallbacks, &Impl::header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(sessionCallbacks, &Impl::dataChunk);
    nghttp2_session_callbacks_set_on_frame_recv_callback(sessionCallbacks, &Impl::frameReceived);
    nghttp2_session_callbacks_set_on_stream_close_callback(sessionCallbacks, &Impl::streamClosed);
    auto result = client
        ? nghttp2_session_client_new(&impl->session, sessionCallbacks, impl.get())
        : nghttp2_session_server_new(&impl->session, sessionCallbacks, impl.get());
    nghttp2_session_callbacks_del(sessionCallbacks);
    if (result != 0) return {};
    nghttp2_settings_entry setting{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, maxConcurrentStreams};
    const auto settingCount = client ? 0 : 1;
    if (!deferOutput && nghttp2_submit_settings(impl->session, NGHTTP2_FLAG_NONE, client ? nullptr : &setting, settingCount) != 0)
    {
        nghttp2_session_del(impl->session);
        return {};
    }
    auto session = Ptr(new Http2Session(std::move(impl)));
    session->impl_->owner = session.get();
    std::weak_ptr<Http2Session> weak = session;
    if (!session->impl_->transport->setWritableCallback([weak] {
            if (auto current = weak.lock()) current->flushOutput();
        })) return {};
    if (!deferOutput) session->impl_->sendPending();
    return session;
}


bool Http2Session::upgradeServer(std::string_view settings, HttpRequest request) noexcept
{
    if (!impl_ || impl_->client || !impl_->streams.empty()) return false;
    if (nghttp2_session_upgrade2(impl_->session, reinterpret_cast<const uint8_t *>(settings.data()),
                                settings.size(), request.method == "HEAD", nullptr) != 0) return false;
    auto state = std::make_unique<Impl::Stream>();
    state->headRequest = request.method == "HEAD";
    state->headNotified = true;
    state->requestNotified = true;
    request.version = "HTTP/2";
    const auto connectionFields=request.headers.getAll("Connection");
    std::vector<std::string> nominated;
    for (auto field : connectionFields) {
        std::size_t offset=0;
        while(offset<field.size()) {
            const auto end=field.find(',',offset);
            auto token=field.substr(offset,end==std::string_view::npos ? field.size()-offset : end-offset);
            while(!token.empty() && (token.front()==' ' || token.front()=='\t')) token.remove_prefix(1);
            while(!token.empty() && (token.back()==' ' || token.back()=='\t')) token.remove_suffix(1);
            if(!token.empty()) nominated.emplace_back(token);
            if(end==std::string_view::npos) break;
            offset=end+1;
        }
    }
    for(const auto &name : nominated) request.headers.remove(name);
    request.headers.set(":scheme","http");
    for (auto token : {"Connection", "Upgrade", "HTTP2-Settings", "Transfer-Encoding", "Keep-Alive", "Proxy-Connection"})
        request.headers.remove(token);
    state->request = std::move(request);
    impl_->streams[1] = std::move(state);
    auto value = std::move(impl_->streams[1]->request);
    if (impl_->callbacks.onRequestBodyWithSession || impl_->callbacks.onRequestBody || impl_->callbacks.onRequestEnd) {
        if (impl_->callbacks.onRequestHead) impl_->callbacks.onRequestHead(shared_from_this(), 1, value);
        if (impl_->callbacks.onRequestBodyWithSession && !value.body.empty()) impl_->callbacks.onRequestBodyWithSession(shared_from_this(),1,value.body);
        else if (impl_->callbacks.onRequestBody && !value.body.empty()) impl_->callbacks.onRequestBody(1, value.body);
        if (impl_->callbacks.onRequestEnd) impl_->callbacks.onRequestEnd(shared_from_this(), 1);
    } else if (impl_->callbacks.onRequest) impl_->callbacks.onRequest(shared_from_this(), 1, std::move(value));
    return impl_->sendPending();
}
Http2Session::Ptr Http2Session::createUpgradeClient(AsyncByteStream::Ptr stream, Callbacks callbacks,
                                                    std::string_view settings, bool headRequest,
                                                    std::size_t maxHeaderBytes, std::size_t maxBodyBytes)
{
    auto session = create(std::move(stream), std::move(callbacks), true, 100, maxBodyBytes, maxHeaderBytes, true);
    if (!session) return {};
    auto state = std::make_unique<Impl::Stream>();
    state->headRequest = headRequest;
    if (nghttp2_session_upgrade2(session->impl_->session, reinterpret_cast<const uint8_t *>(settings.data()),
                                settings.size(), headRequest, state.get()) != 0) return {};
    session->impl_->streams[1] = std::move(state);
    if (!session->impl_->sendPending()) return {};
    return session;
}

bool Http2Session::receive(std::string_view bytes) noexcept
{
    if (!impl_ || impl_->failed) return false;
    impl_->processingInput = true;
    const auto consumed = nghttp2_session_mem_recv(impl_->session,
        reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    impl_->processingInput = false;
    if (consumed < 0 || static_cast<std::size_t>(consumed) != bytes.size())
    {
        const int errorCode = consumed < 0 ? static_cast<int>(consumed) : static_cast<int>(NGHTTP2_ERR_PROTO);
        impl_->report(errorCode, "nghttp2_session_mem_recv");
        return false;
    }
    return impl_->sendPending();
}

std::int32_t Http2Session::request(HttpRequest requestValue) noexcept
{
    if (!impl_ || impl_->failed || !impl_->client) return -1;
    auto state = std::make_unique<Impl::Stream>();
    state->request = std::move(requestValue);
    auto &request = state->request;
    const auto authority = request.headers.get("host");
    std::vector<std::string> names, values;
    auto add = [&names, &values](std::string name, std::string value) {
        names.emplace_back(std::move(name));
        values.emplace_back(std::move(value));
    };
    add(":method", request.method); add(":path", request.target);
    const auto scheme = request.headers.get(":scheme");
    add(":scheme", scheme.empty() ? "https" : std::string(scheme));
    add(":authority", std::string(authority));
    for (const auto &field : request.headers.fields())
    {
        auto name = lowerName(field.first);
        if (!name.empty() && name != "host" && !forbiddenHeader(name) && name.front() != ':') add(std::move(name), field.second);
    }
    std::vector<nghttp2_nv> headers;
    headers.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        nghttp2_nv nv{};
        nv.name = reinterpret_cast<uint8_t *>(names[index].data());
        nv.namelen = names[index].size();
        nv.value = reinterpret_cast<uint8_t *>(values[index].data());
        nv.valuelen = values[index].size();
        nv.flags = NGHTTP2_NV_FLAG_NONE;
        headers.push_back(nv);
    }
    nghttp2_data_provider provider{};
    nghttp2_data_provider *providerPtr = nullptr;
    if (!request.body.empty())
    {
        provider.source.ptr = state.get();
        provider.read_callback = &Impl::readData;
        providerPtr = &provider;
    }
    const auto id = nghttp2_submit_request(impl_->session, nullptr, headers.data(), headers.size(), providerPtr, state.get());
    if (id < 0) { impl_->report(static_cast<int>(id), "nghttp2_submit_request"); return -1; }
    state->responseSide = false;
    impl_->streams[id] = std::move(state);
    if (!impl_->sendPending()) return -1;
    return id;
}

std::int32_t Http2Session::requestStream(HttpRequestHead requestValue) noexcept
{
    if (!impl_ || impl_->failed || !impl_->client) return -1;
    auto state = std::make_unique<Impl::Stream>();
    static_cast<HttpRequestHead &>(state->request) = std::move(requestValue);
    state->outboundStreaming = true;
    auto &request = state->request;
    const auto authority = request.headers.get("host");
    std::vector<std::string> names, values;
    auto add = [&names, &values](std::string name, std::string value) { names.emplace_back(std::move(name)); values.emplace_back(std::move(value)); };
    add(":method", request.method); add(":path", request.target);
    const auto scheme = request.headers.get(":scheme");
    add(":scheme", scheme.empty() ? "https" : std::string(scheme));
    add(":authority", std::string(authority));
    for (const auto &field : request.headers.fields()) { auto name = lowerName(field.first); if (name != "host" && !forbiddenHeader(name) && !name.empty() && name.front() != ':') add(std::move(name), field.second); }
    std::vector<nghttp2_nv> headers;
    headers.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index) { nghttp2_nv nv{}; nv.name = reinterpret_cast<uint8_t *>(names[index].data()); nv.namelen = names[index].size(); nv.value = reinterpret_cast<uint8_t *>(values[index].data()); nv.valuelen = values[index].size(); headers.push_back(nv); }
    nghttp2_data_provider provider{}; provider.source.ptr = state.get(); provider.read_callback = &Impl::readData;
    const auto id = nghttp2_submit_request(impl_->session, nullptr, headers.data(), headers.size(), &provider, state.get());
    if (id < 0) { impl_->report(static_cast<int>(id), "nghttp2_submit_request"); return -1; }
    state->responseSide = false;
    impl_->streams[id] = std::move(state);
    if (!impl_->sendPending()) return -1;
    return id;
}
bool Http2Session::respondHead(std::int32_t streamId, HttpResponseHead responseValue) noexcept
{
    if (!impl_ || impl_->client) return false;
    if (!impl_->transport->isLoopThread())
    {
        auto self = shared_from_this();
        return impl_->transport->post([self, streamId, responseValue = std::move(responseValue)]() mutable {
            self->respondHead(streamId, std::move(responseValue));
        });
    }
    if (impl_->failed) return false;
    auto found = impl_->streams.find(streamId);
    if (found == impl_->streams.end()) return false;
    auto &state = *found->second;
    if (state.responseSubmitted || responseValue.status < 200 || responseValue.status > 599) return false;
    state.suppressBody = state.headRequest || responseValue.status == 204 || responseValue.status == 304;
    if (responseValue.status == 204) responseValue.headers.remove("content-length");
    state.responseSubmitted = true;
    state.response = HttpResponse{};
    static_cast<HttpResponseHead &>(state.response) = std::move(responseValue);
    state.responseSide = true;
    state.outboundStreaming = true;
    std::vector<std::string> names, values;
    names.emplace_back(":status"); values.emplace_back(std::to_string(state.response.status));
    for (const auto &field : state.response.headers.fields()) {
        auto name = lowerName(field.first);
        if (!forbiddenHeader(name) && name != "host" && !name.empty() && name.front() != ':') { names.emplace_back(std::move(name)); values.push_back(field.second); }
    }
    std::vector<nghttp2_nv> headers;
    for (std::size_t i=0; i<names.size(); ++i) { nghttp2_nv nv{}; nv.name=reinterpret_cast<uint8_t*>(names[i].data()); nv.namelen=names[i].size(); nv.value=reinterpret_cast<uint8_t*>(values[i].data()); nv.valuelen=values[i].size(); headers.push_back(nv); }
    nghttp2_data_provider provider{}; provider.source.ptr=&state; provider.read_callback=&Impl::readData;
    const auto result=nghttp2_submit_response(impl_->session,streamId,headers.data(),headers.size(),state.suppressBody ? nullptr : &provider);
    if(result!=0){impl_->report(result,"nghttp2_submit_response");return false;}
    return impl_->processingInput ? true : impl_->sendPending();
}

bool Http2Session::writeData(std::int32_t streamId, std::string chunk) noexcept
{
    if (!impl_ || impl_->failed || impl_->client || chunk.empty()) return false;
    auto found=impl_->streams.find(streamId); if(found==impl_->streams.end() || !found->second->outboundStreaming || found->second->outboundEnded) return false;
    if (found->second->suppressBody) return true;
    found->second->outbound.push_back(std::move(chunk));
    const auto result=nghttp2_session_resume_data(impl_->session,streamId);
    if(result!=0 && result!=NGHTTP2_ERR_INVALID_ARGUMENT){impl_->report(result,"nghttp2_session_resume_data");return false;}
    return impl_->processingInput ? true : impl_->sendPending();
}
bool Http2Session::respond(std::int32_t streamId, HttpResponse responseValue) noexcept
{
    if (!impl_ || impl_->client) return false;
    if (!impl_->transport->isLoopThread())
    {
        auto self = shared_from_this();
        return impl_->transport->post([self, streamId, responseValue = std::move(responseValue)]() mutable {
            self->respond(streamId, std::move(responseValue));
        });
    }
    if (impl_->failed) return false;
    auto found = impl_->streams.find(streamId);
    if (found == impl_->streams.end()) return false;
    auto &state = *found->second;
    if (state.responseSubmitted || responseValue.status < 100 || responseValue.status > 599 || responseValue.status == 101) return false;
    const bool informational = responseValue.status < 200;
    state.suppressBody = state.headRequest || responseValue.status == 204 || responseValue.status == 304 || informational;
    if (responseValue.status == 204 || informational) responseValue.headers.remove("content-length");
    if (state.suppressBody) responseValue.body.clear();
    state.responseSubmitted = !informational;
    state.response = std::move(responseValue);
    state.responseSide = true;
    std::vector<std::string> names, values;
    names.emplace_back(":status"); values.emplace_back(std::to_string(state.response.status));
    for (const auto &field : state.response.headers.fields())
    {
        auto name = lowerName(field.first);
        if (!forbiddenHeader(name) && name != "host" && !name.empty() && name.front() != ':')
        { names.emplace_back(std::move(name)); values.push_back(field.second); }
    }
    std::vector<nghttp2_nv> headers;
    headers.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        nghttp2_nv nv{};
        nv.name = reinterpret_cast<uint8_t *>(names[i].data()); nv.namelen = names[i].size();
        nv.value = reinterpret_cast<uint8_t *>(values[i].data()); nv.valuelen = values[i].size();
        nv.flags = NGHTTP2_NV_FLAG_NONE; headers.push_back(nv);
    }
    nghttp2_data_provider provider{};
    nghttp2_data_provider *providerPtr = nullptr;
    if (!state.response.body.empty())
    { provider.source.ptr = &state; provider.read_callback = &Impl::readData; providerPtr = &provider; }
    const auto result = informational
        ? nghttp2_submit_headers(impl_->session, NGHTTP2_FLAG_NONE, streamId, nullptr, headers.data(), headers.size(), nullptr)
        : nghttp2_submit_response(impl_->session, streamId, headers.data(), headers.size(), providerPtr);
    if (result != 0) { impl_->report(result, "nghttp2_submit_response"); return false; }
    return impl_->processingInput ? true : impl_->sendPending();
}

void Http2Session::flushOutput() noexcept
{
    if (!impl_ || impl_->failed) return;
    while (!impl_->output.empty())
    {
        auto status = impl_->transport->writeBytes(impl_->output.front());
        if (status == AsyncByteStream::WriteStatus::Backpressured) return;
        if (status == AsyncByteStream::WriteStatus::Closed)
        { impl_->report(NGHTTP2_ERR_CALLBACK_FAILURE, "HTTP/2 transport write failed"); return; }
        impl_->outputBytes -= impl_->output.front().size();
        impl_->output.pop_front();
    }
}

bool Http2Session::endData(std::int32_t streamId) noexcept
{
    if (!impl_ || impl_->failed || impl_->client) return false;
    auto found=impl_->streams.find(streamId); if(found==impl_->streams.end() || !found->second->outboundStreaming) return false;
    if (found->second->outboundEnded) return false;
    found->second->outboundEnded=true;
    if (found->second->suppressBody) return true;
    const auto result=nghttp2_session_resume_data(impl_->session,streamId);
    if(result!=0 && result!=NGHTTP2_ERR_INVALID_ARGUMENT){impl_->report(result,"nghttp2_session_resume_data");return false;}
    return impl_->processingInput ? true : impl_->sendPending();
}
bool Http2Session::writeRequestData(std::int32_t streamId, std::string chunk) noexcept
{
    if (!impl_ || impl_->failed || !impl_->client || chunk.empty()) return false;
    auto found = impl_->streams.find(streamId);
    if (found == impl_->streams.end() || !found->second->outboundStreaming || found->second->outboundEnded) return false;
    found->second->outbound.push_back(std::move(chunk));
    const auto result = nghttp2_session_resume_data(impl_->session, streamId);
    if (result != 0 && result != NGHTTP2_ERR_INVALID_ARGUMENT) { impl_->report(result, "nghttp2_session_resume_data"); return false; }
    return impl_->processingInput ? true : impl_->sendPending();
}

bool Http2Session::endRequestData(std::int32_t streamId) noexcept
{
    if (!impl_ || impl_->failed || !impl_->client) return false;
    auto found = impl_->streams.find(streamId);
    if (found == impl_->streams.end() || !found->second->outboundStreaming || found->second->outboundEnded) return false;
    found->second->outboundEnded = true;
    const auto result = nghttp2_session_resume_data(impl_->session, streamId);
    if (result != 0 && result != NGHTTP2_ERR_INVALID_ARGUMENT) { impl_->report(result, "nghttp2_session_resume_data"); return false; }
    return impl_->processingInput ? true : impl_->sendPending();
}
bool Http2Session::goAway(std::int32_t lastStreamId, std::uint32_t errorCode) noexcept
{
    if (!impl_ || impl_->failed || lastStreamId < 0) return false;
    const auto result = nghttp2_submit_goaway(impl_->session, NGHTTP2_FLAG_NONE, lastStreamId, errorCode, nullptr, 0);
    if (result != 0) { impl_->report(result, "nghttp2_submit_goaway"); return false; }
    return impl_->processingInput ? true : impl_->sendPending();
}

bool Http2Session::resetStream(std::int32_t streamId, std::uint32_t errorCode) noexcept
{
    if (!impl_ || impl_->failed) return false;
    if (!impl_->transport->isLoopThread())
    {
        auto self = shared_from_this();
        return impl_->transport->post([self, streamId, errorCode] { self->resetStream(streamId, errorCode); });
    }
    const auto result = nghttp2_submit_rst_stream(impl_->session, NGHTTP2_FLAG_NONE, streamId, errorCode);
    if (result != 0) return false;
    return impl_->processingInput ? true : impl_->sendPending();
}

bool Http2Session::isClient() const noexcept { return impl_ && impl_->client; }

}
