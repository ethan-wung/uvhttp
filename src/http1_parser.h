/**
 * @file        http1_parser.h
 * @brief       HTTP/1.x 内部增量解析器，处理消息定界与解析状态；内部实现，不作为公共接口。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP1_PARSER_H
#define UVHTTP_HTTP1_PARSER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "http_internal.h"
#include "uvhttp/error.h"

namespace uvhttp::detail
{

struct Http1MessageHead
{
    bool request{false};
    HttpRequestHead requestHead;
    HttpResponseHead responseHead;
};

class Http1Parser
{
  public:
    struct Callbacks
    {
        std::function<void(const Http1MessageHead &)> onHead;
        std::function<void(std::string_view)> onBody;
        std::function<void()> onComplete;
        std::function<void(const Error &)> onError;
    };

    enum class Kind { Request, Response };

    Http1Parser(Kind kind, std::size_t maxHeaderBytes, Callbacks callbacks)
        : kind_(kind), maxHeaderBytes_(maxHeaderBytes), callbacks_(std::move(callbacks)) {}

    void feed(std::string_view bytes)
    {
        if (failed_) return;
        buffer_.append(bytes.data(), bytes.size());
        if (!complete_) process(false);
    }

    void finish()
    {
        if (failed_ || complete_) return;
        process(true);
    }

    void setResponseToHead(bool value) noexcept { responseToHead_ = value; }

    void resume()
    {
        if (failed_ || !complete_) return;
        complete_ = false;
        state_ = State::StartLine;
        cursor_ = 0;
        headerBytes_ = 0;
        remaining_ = 0;
        message_ = Http1MessageHead{};
        process(false);
    }

    std::string takeBuffered()
    {
        if (cursor_ != 0)
        {
            buffer_.erase(0, cursor_);
            cursor_ = 0;
        }
        std::string result = std::move(buffer_);
        buffer_.clear();
        return result;
    }

    bool complete() const noexcept { return complete_; }
    bool failed() const noexcept { return failed_; }
    bool hasBufferedInput() const noexcept { return cursor_ < buffer_.size(); }

  private:
    enum class State { StartLine, Headers, FixedBody, ChunkSize, ChunkBody, ChunkCrlf,
                        Trailers, CloseDelimited, Complete, Failed };

    void fail(std::string message)
    {
        if (failed_ || complete_) return;
        failed_ = true;
        state_ = State::Failed;
        if (callbacks_.onError) callbacks_.onError(Error{UV_EPROTO, std::move(message)});
    }

    bool readLine(std::string_view &line, bool eof)
    {
        const auto end = buffer_.find("\r\n", cursor_);
        if (end == std::string::npos)
        {
            if (buffer_.size() - cursor_ > maxHeaderBytes_ || eof)
                fail(eof ? "unexpected EOF in HTTP line" : "HTTP line/header limit exceeded");
            return false;
        }
        const std::size_t length = end - cursor_;
        if (length + 2 > maxHeaderBytes_ || headerBytes_ > maxHeaderBytes_ - (length + 2))
        {
            fail("HTTP header limit exceeded");
            return false;
        }
        headerBytes_ += length + 2;
        line = std::string_view(buffer_.data() + cursor_, length);
        cursor_ = end + 2;
        return true;
    }

    bool parseStartLine(std::string_view line)
    {
        if (line.find_first_of("\r\n") != std::string_view::npos || line.find(char{0}) != std::string_view::npos)
        {
            fail("invalid HTTP start line");
            return false;
        }
        if (kind_ == Kind::Request)
        {
            const auto first = line.find(' ');
            const auto second = first == std::string_view::npos ? first : line.find(' ', first + 1);
            if (first == std::string_view::npos || second == std::string_view::npos ||
                line.find(' ', second + 1) != std::string_view::npos)
            {
                fail("malformed HTTP request line");
                return false;
            }
            const auto method = line.substr(0, first);
            const auto target = line.substr(first + 1, second - first - 1);
            const auto version = line.substr(second + 1);
            if (!isHttpToken(method) || target.empty() ||
                (version != "HTTP/1.0" && version != "HTTP/1.1"))
            {
                fail("unsupported or malformed HTTP request line");
                return false;
            }
            message_.request = true;
            message_.requestHead.method.assign(method);
            message_.requestHead.target.assign(target);
            message_.requestHead.version.assign(version);
        }
        else
        {
            const auto first = line.find(' ');
            const auto second = first == std::string_view::npos ? first : line.find(' ', first + 1);
            if (first == std::string_view::npos || first == 0)
            {
                fail("malformed HTTP status line");
                return false;
            }
            const auto version = line.substr(0, first);
            const auto statusText = second == std::string_view::npos
                                        ? line.substr(first + 1)
                                        : line.substr(first + 1, second - first - 1);
            if ((version != "HTTP/1.0" && version != "HTTP/1.1") || statusText.size() != 3 ||
                !std::all_of(statusText.begin(), statusText.end(), [](char c) { return c >= '0' && c <= '9'; }))
            {
                fail("unsupported or malformed HTTP status line");
                return false;
            }
            message_.responseHead.version.assign(version);
            message_.responseHead.status = (statusText[0] - '0') * 100 +
                                           (statusText[1] - '0') * 10 + statusText[2] - '0';
            if (message_.responseHead.status < 100 || message_.responseHead.status > 599)
            {
                fail("invalid HTTP status code");
                return false;
            }
            if (second != std::string_view::npos)
                message_.responseHead.reason.assign(line.substr(second + 1));
            if (!isValidHeaderValue(message_.responseHead.reason))
            {
                fail("invalid HTTP reason phrase");
                return false;
            }
        }
        state_ = State::Headers;
        return true;
    }

    HttpHeaders &headers()
    {
        return kind_ == Kind::Request ? message_.requestHead.headers : message_.responseHead.headers;
    }

    bool parseHeader(std::string_view line)
    {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || !isHttpToken(line.substr(0, colon)))
        {
            fail("malformed HTTP header field");
            return false;
        }
        const auto value = trimOws(line.substr(colon + 1));
        if (!isValidHeaderValue(value))
        {
            fail("invalid HTTP header value");
            return false;
        }
        headers().add(std::string(line.substr(0, colon)), std::string(value));
        return true;
    }

    bool validateTrailer(std::string_view line)
    {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos || !isHttpToken(line.substr(0, colon)) ||
            !isValidHeaderValue(trimOws(line.substr(colon + 1))))
        {
            fail("malformed HTTP trailer field");
            return false;
        }
        const auto name = lowerAscii(line.substr(0, colon));
        if (name == "content-length" || name == "transfer-encoding" || name == "host")
        {
            fail("forbidden field in HTTP trailer");
            return false;
        }
        return true;
    }

    bool beginBody()
    {
        const auto contentLengths = headers().getAll("Content-Length");
        const auto transferEncodings = headers().getAll("Transfer-Encoding");
        if (!contentLengths.empty() && !transferEncodings.empty())
        {
            fail("Content-Length conflicts with Transfer-Encoding");
            return false;
        }
        if (transferEncodings.size() > 1)
        {
            fail("multiple Transfer-Encoding fields are unsupported");
            return false;
        }
        if (kind_ == Kind::Response &&
            (responseToHead_ || (message_.responseHead.status >= 100 && message_.responseHead.status < 200) ||
             message_.responseHead.status == 204 || message_.responseHead.status == 304))
        {
            state_ = State::Complete;
        }
        else if (!transferEncodings.empty())
        {
            if (lowerAscii(trimOws(transferEncodings.front())) != "chunked")
            {
                fail("unsupported Transfer-Encoding");
                return false;
            }
            state_ = State::ChunkSize;
        }
        else if (!contentLengths.empty())
        {
            std::size_t length = 0;
            bool haveLength = false;
            for (const auto value : contentLengths)
            {
                const auto text = trimOws(value);
                if (text.empty())
                {
                    fail("empty Content-Length");
                    return false;
                }
                std::size_t parsed = 0;
                for (char c : text)
                {
                    if (c < '0' || c > '9' ||
                        parsed > (std::numeric_limits<std::size_t>::max() - (c - '0')) / 10)
                    {
                        fail("invalid Content-Length");
                        return false;
                    }
                    parsed = parsed * 10 + static_cast<std::size_t>(c - '0');
                }
                if (haveLength && parsed != length)
                {
                    fail("conflicting Content-Length fields");
                    return false;
                }
                length = parsed;
                haveLength = true;
            }
            remaining_ = length;
            state_ = remaining_ == 0 ? State::Complete : State::FixedBody;
        }
        else if (kind_ == Kind::Response && message_.responseHead.status >= 200)
        {
            state_ = State::CloseDelimited;
        }
        else
        {
            state_ = State::Complete;
        }
        if (callbacks_.onHead) callbacks_.onHead(message_);
        if (kind_ == Kind::Response && message_.responseHead.status >= 100 &&
            message_.responseHead.status < 200 && message_.responseHead.status != 101)
        {
            message_ = Http1MessageHead{};
            headerBytes_ = 0;
            state_ = State::StartLine;
            return !failed_;
        }
        if (state_ == State::Complete) completeMessage();
        return !failed_;
    }

    void emitBody(std::size_t length)
    {
        if (length == 0) return;
        if (callbacks_.onBody)
            callbacks_.onBody(std::string_view(buffer_.data() + cursor_, length));
        cursor_ += length;
    }

    void completeMessage()
    {
        state_ = State::Complete;
        complete_ = true;
        if (callbacks_.onComplete) callbacks_.onComplete();
    }

    void compact()
    {
        if (cursor_ != 0)
        {
            buffer_.erase(0, cursor_);
            cursor_ = 0;
        }
    }

    void process(bool eof)
    {
        while (!failed_ && !complete_)
        {
            if (state_ == State::StartLine || state_ == State::Headers ||
                state_ == State::ChunkSize || state_ == State::Trailers)
            {
                std::string_view line;
                if (!readLine(line, eof)) break;
                if (state_ == State::StartLine)
                {
                    if (!parseStartLine(line)) break;
                }
                else if (state_ == State::Headers)
                {
                    if (line.empty())
                    {
                        if (!beginBody()) break;
                    }
                    else if (!parseHeader(line)) break;
                }
                else if (state_ == State::ChunkSize)
                {
                    const auto semicolon = line.find(';');
                    const auto sizeText = line.substr(0, semicolon);
                    if (sizeText.empty()) { fail("empty chunk size"); break; }
                    std::size_t size = 0;
                    for (char c : sizeText)
                    {
                        unsigned int digit;
                        if (c >= '0' && c <= '9') digit = static_cast<unsigned int>(c - '0');
                        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned int>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned int>(c - 'A' + 10);
                        else { fail("invalid chunk size"); break; }
                        if (size > (std::numeric_limits<std::size_t>::max() - digit) / 16)
                        { fail("chunk size overflow"); break; }
                        size = size * 16 + digit;
                    }
                    if (failed_) break;
                    remaining_ = size;
                    state_ = size == 0 ? State::Trailers : State::ChunkBody;
                }
                else
                {
                    if (line.empty()) completeMessage();
                    else if (!validateTrailer(line)) break;
                }
                continue;
            }
            if (state_ == State::FixedBody || state_ == State::ChunkBody)
            {
                const std::size_t available = buffer_.size() - cursor_;
                if (available == 0) break;
                const std::size_t count = std::min(available, remaining_);
                emitBody(count);
                remaining_ -= count;
                if (remaining_ == 0)
                    state_ = state_ == State::FixedBody ? State::Complete : State::ChunkCrlf;
                if (state_ == State::Complete) completeMessage();
                continue;
            }
            if (state_ == State::ChunkCrlf)
            {
                if (buffer_.size() - cursor_ < 2) break;
                if (buffer_[cursor_] != '\r' || buffer_[cursor_ + 1] != '\n')
                { fail("missing CRLF after chunk data"); break; }
                cursor_ += 2;
                state_ = State::ChunkSize;
                continue;
            }
            if (state_ == State::CloseDelimited)
            {
                emitBody(buffer_.size() - cursor_);
                if (eof) completeMessage();
                break;
            }
        }
        compact();
        if (eof && !failed_ && !complete_ && state_ != State::CloseDelimited)
            fail("unexpected EOF before HTTP message completed");
        else if (eof && state_ == State::CloseDelimited && !complete_)
        {
            complete_ = true;
            state_ = State::Complete;
            if (callbacks_.onComplete) callbacks_.onComplete();
        }
    }

    Kind kind_;
    std::size_t maxHeaderBytes_;
    Callbacks callbacks_;
    State state_{State::StartLine};
    std::string buffer_;
    std::size_t cursor_{0};
    std::size_t headerBytes_{0};
    std::size_t remaining_{0};
    Http1MessageHead message_;
    bool complete_{false};
    bool failed_{false};
    bool responseToHead_{false};
};

}  // namespace uvhttp::detail

#endif
