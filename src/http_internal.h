/**
 * @file        http_internal.h
 * @brief       HTTP/1.x 内部头字段校验与报文序列化辅助接口；内部实现，不作为公共接口。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP_INTERNAL_H
#define UVHTTP_HTTP_INTERNAL_H

#include <cstdint>
#include <string>
#include <string_view>

#include "uvhttp/http.h"

namespace uvhttp::detail
{

bool isHttpToken(std::string_view value) noexcept;
bool isValidHeaderValue(std::string_view value) noexcept;
std::string lowerAscii(std::string_view value);
std::string_view trimOws(std::string_view value) noexcept;
bool hasConnectionToken(const HttpHeaders &headers, std::string_view token) noexcept;
std::string reasonPhrase(int status);
bool serializeRequestHead(const HttpRequestHead &request,
                          std::string_view host,
                          std::uint16_t port,
                          bool chunked,
                          bool keepAlive,
                          std::string &output);
bool serializeResponseHead(const HttpResponseHead &response,
                           bool chunked,
                           bool keepAlive,
                           std::string &output);

}  // namespace uvhttp::detail

#endif
