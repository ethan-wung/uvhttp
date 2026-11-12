/**
 * @file        http.cc
 * @brief       HTTP 请求、响应、头字段以及流式读写器的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/http.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <mutex>
#include <sstream>
#include <utility>

#include "uvhttp/tcp_connection.h"
#include "uvhttp/async_byte_stream.h"
#include "http_internal.h"

namespace uvhttp::detail
{

struct HttpResponseWriterState
{
    enum class Mode { Initial, Chunked, Finished };
    explicit HttpResponseWriterState(AsyncByteStream::Ptr value,
                                     bool head,
                                     bool keep,
                                     std::function<void()> finished)
        : stream(std::move(value)), headRequest(head), keepAlive(keep), onFinished(std::move(finished)) {}
    std::mutex mutex;
    AsyncByteStream::Ptr stream;
    Mode mode{Mode::Initial};
    bool headRequest{false};
    bool suppressBody{false};
    bool keepAlive{false};
    std::function<void()> onFinished;
};

struct HttpRequestBodyWriterState
{
    explicit HttpRequestBodyWriterState(AsyncByteStream::Ptr value)
        : stream(std::move(value)) {}
    std::mutex mutex;
    AsyncByteStream::Ptr stream;
    bool ended{false};
};

bool isHttpToken(std::string_view value) noexcept
{
    if (value.empty()) return false;
    for (unsigned char c : value)
    {
        if (std::isalnum(c)) continue;
        switch (c)
        {
            case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
            case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
                continue;
            default: return false;
        }
    }
    return true;
}

bool isValidHeaderValue(std::string_view value) noexcept
{
    for (unsigned char c : value)
    {
        if (c == '\r' || c == '\n' || c == 0 || (c < 0x20 && c != '\t') || c == 0x7f)
            return false;
    }
    return true;
}

std::string lowerAscii(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string_view trimOws(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}

bool hasConnectionToken(const HttpHeaders &headers, std::string_view token) noexcept
{
    for (const auto value : headers.getAll("Connection"))
    {
        std::size_t begin = 0;
        while (begin <= value.size())
        {
            const auto end = value.find(',', begin);
            if (lowerAscii(trimOws(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin))) == lowerAscii(token))
                return true;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
    }
    return false;
}

std::string reasonPhrase(int status)
{
    switch (status)
    {
        case 100: return "Continue";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Content Too Large";
        case 415: return "Unsupported Media Type";
        case 417: return "Expectation Failed";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "Unknown";
    }
}

static bool appendHeaders(const HttpHeaders &headers, std::string &output)
{
    for (const auto &field : headers.fields())
    {
        if (!isHttpToken(field.first) || !isValidHeaderValue(field.second)) return false;
        output.append(field.first).append(": ").append(field.second).append("\r\n");
    }
    return true;
}

bool serializeRequestHead(const HttpRequestHead &request,
                          std::string_view host,
                          std::uint16_t port,
                          bool chunked,
                          bool keepAlive,
                          std::string &output)
{
    if (!isHttpToken(request.method) || request.target.empty() ||
        request.target.find_first_of(" \r\n") != std::string::npos ||
        (request.version != "HTTP/1.1" && request.version != "HTTP/1.0")) return false;
    HttpHeaders headers = request.headers;
    if (!headers.contains("Host"))
    {
        std::string hostValue(host);
        if (hostValue.find(':') != std::string::npos && (hostValue.empty() || hostValue.front() != '['))
            hostValue = "[" + hostValue + "]";
        if (port != 80) hostValue += ":" + std::to_string(port);
        headers.set("Host", std::move(hostValue));
    }
    if (!headers.contains("Connection"))
        headers.set("Connection", keepAlive ? "keep-alive" : "close");
    if (chunked)
    {
        headers.remove("Content-Length");
        headers.set("Transfer-Encoding", "chunked");
    }
    else
    {
        headers.remove("Transfer-Encoding");
        if (!headers.contains("Content-Length"))
            headers.set("Content-Length", "0");
    }
    output = request.method + " " + request.target + " " + request.version + "\r\n";
    if (!appendHeaders(headers, output)) return false;
    output.append("\r\n");
    return true;
}

bool serializeResponseHead(const HttpResponseHead &response,
                           bool chunked,
                           bool keepAlive,
                           std::string &output)
{
    if (response.status < 100 || response.status > 999 ||
        (response.version != "HTTP/1.1" && response.version != "HTTP/1.0")) return false;
    HttpHeaders headers = response.headers;
    if (chunked)
    {
        headers.remove("Content-Length");
        headers.set("Transfer-Encoding", "chunked");
    }
    else
        headers.remove("Transfer-Encoding");
    if (!headers.contains("Connection"))
        headers.set("Connection", keepAlive ? "keep-alive" : "close");
    const std::string reason = response.reason.empty() ? reasonPhrase(response.status) : response.reason;
    if (!isValidHeaderValue(reason)) return false;
    output = response.version + " " + std::to_string(response.status) + " " + reason + "\r\n";
    if (!appendHeaders(headers, output)) return false;
    output.append("\r\n");
    return true;
}

}  // namespace uvhttp::detail

namespace uvhttp
{

static bool equalHeaderName(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

void HttpHeaders::add(std::string name, std::string value)
{
    fields_.emplace_back(std::move(name), std::move(value));
}

void HttpHeaders::set(std::string name, std::string value)
{
    remove(name);
    add(std::move(name), std::move(value));
}

bool HttpHeaders::remove(std::string_view name)
{
    const auto oldSize = fields_.size();
    fields_.erase(std::remove_if(fields_.begin(), fields_.end(), [&](const Field &field) {
        return equalHeaderName(field.first, name);
    }), fields_.end());
    return fields_.size() != oldSize;
}

bool HttpHeaders::contains(std::string_view name) const noexcept
{
    return std::any_of(fields_.begin(), fields_.end(), [&](const Field &field) {
        return equalHeaderName(field.first, name);
    });
}

std::string_view HttpHeaders::get(std::string_view name) const noexcept
{
    for (const auto &field : fields_) if (equalHeaderName(field.first, name)) return field.second;
    return {};
}

std::vector<std::string_view> HttpHeaders::getAll(std::string_view name) const
{
    std::vector<std::string_view> values;
    for (const auto &field : fields_) if (equalHeaderName(field.first, name)) values.emplace_back(field.second);
    return values;
}

const HttpHeaders::Container &HttpHeaders::fields() const noexcept { return fields_; }
bool HttpHeaders::empty() const noexcept { return fields_.empty(); }

HttpResponseWriter::HttpResponseWriter(std::shared_ptr<detail::HttpResponseWriterState> state)
    : state_(std::move(state)) {}

HttpResponseWriter HttpResponseWriter::fromStream(AsyncByteStream::Ptr stream,
                                                bool headRequest,
                                                bool keepAlive,
                                                std::function<void()> onFinished)
{
    return HttpResponseWriter(std::make_shared<detail::HttpResponseWriterState>(
        std::move(stream), headRequest, keepAlive, std::move(onFinished)));
}

HttpResponseWriter::operator bool() const noexcept { return static_cast<bool>(state_); }

bool HttpResponseWriter::send(HttpResponse response) const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->mode != detail::HttpResponseWriterState::Mode::Initial) return false;
    HttpResponseHead head = response;
    if (response.status == 204 ||
        (response.status >= 100 && response.status < 200))
        head.headers.remove("Content-Length");
    else if (response.status != 304)
        head.headers.set("Content-Length", std::to_string(response.body.size()));
    std::string bytes;
    if (!detail::serializeResponseHead(head, false, state_->keepAlive, bytes)) return false;
    if (!state_->headRequest && response.status != 204 && response.status != 304 &&
        (response.status < 100 || response.status >= 200)) bytes += response.body;
    if (state_->stream->writeBytes(std::move(bytes)) != AsyncByteStream::WriteStatus::Accepted)
        return false;
    if (response.status >= 100 && response.status < 200 && response.status != 101)
        return true;
    state_->mode = detail::HttpResponseWriterState::Mode::Finished;
    if (state_->onFinished) state_->onFinished();
    else if (!state_->keepAlive) state_->stream->shutdownWrite();
    return true;
}

bool HttpResponseWriter::beginChunked(HttpResponseHead response) const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->mode != detail::HttpResponseWriterState::Mode::Initial) return false;
    if (response.status < 200) return false;
    const bool suppressBody = state_->headRequest || response.status == 204 || response.status == 304;
    if (response.status == 204) response.headers.remove("Content-Length");
    std::string bytes;
    if (!detail::serializeResponseHead(response, !suppressBody, state_->keepAlive, bytes)) return false;
    if (state_->stream->writeBytes(std::move(bytes)) != AsyncByteStream::WriteStatus::Accepted) return false;
    state_->suppressBody = suppressBody;
    state_->mode = detail::HttpResponseWriterState::Mode::Chunked;
    return true;
}

static std::string makeChunk(std::string_view data)
{
    char sizeBuffer[2 * sizeof(std::size_t) + 1];
    auto converted = std::to_chars(sizeBuffer, sizeBuffer + sizeof(sizeBuffer), data.size(), 16);
    std::string result(sizeBuffer, converted.ptr);
    result.append("\r\n").append(data).append("\r\n");
    return result;
}

bool HttpResponseWriter::write(std::string chunk) const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->mode != detail::HttpResponseWriterState::Mode::Chunked) return false;
    if (chunk.empty() || state_->suppressBody) return true;
    return state_->stream->writeBytes(makeChunk(chunk)) == AsyncByteStream::WriteStatus::Accepted;
}

bool HttpResponseWriter::end() const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->mode != detail::HttpResponseWriterState::Mode::Chunked) return false;
    const std::string terminator = state_->suppressBody ? std::string{} : std::string("0\r\n\r\n");
    if (state_->stream->writeBytes(terminator) != AsyncByteStream::WriteStatus::Accepted) return false;
    state_->mode = detail::HttpResponseWriterState::Mode::Finished;
    if (state_->onFinished) state_->onFinished();
    else if (!state_->keepAlive) state_->stream->shutdownWrite();
    return true;
}

HttpRequestBodyWriter::HttpRequestBodyWriter(std::shared_ptr<detail::HttpRequestBodyWriterState> state)
    : state_(std::move(state)) {}

HttpRequestBodyWriter HttpRequestBodyWriter::fromStream(AsyncByteStream::Ptr stream)
{
    return HttpRequestBodyWriter(std::make_shared<detail::HttpRequestBodyWriterState>(std::move(stream)));
}

HttpRequestBodyWriter::operator bool() const noexcept { return static_cast<bool>(state_); }

bool HttpRequestBodyWriter::write(std::string chunk) const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->ended) return false;
    if (chunk.empty()) return true;
    return state_->stream->writeBytes(makeChunk(chunk)) == AsyncByteStream::WriteStatus::Accepted;
}

bool HttpRequestBodyWriter::end() const
{
    if (!state_) return false;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->ended) return false;
    if (state_->stream->writeBytes("0\r\n\r\n") != AsyncByteStream::WriteStatus::Accepted) return false;
    state_->ended = true;
    state_->stream->shutdownWrite();
    return true;
}

}  // namespace uvhttp
