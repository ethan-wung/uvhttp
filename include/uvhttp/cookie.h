/**
 * @file        cookie.h
 * @brief       Cookie 数据结构、请求 Cookie 解析与响应序列化
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_COOKIE_H
#define UVHTTP_COOKIE_H
#include <string>
#include <string_view>
#include <vector>
#include "uvhttp/export.h"
namespace uvhttp
{
/**
 * @brief       Cookie 数据结构、请求 Cookie 解析与响应序列化。
 */
struct UVHTTP_EXPORT Cookie
{
    /**
     * @brief       Cookie 名称。
     */
    std::string name;
    /**
     * @brief       Cookie 值。
     */
    std::string value;
    /**
     * @brief       Cookie 生效路径。
     */
    std::string path;
    /**
     * @brief       Cookie 生效域。
     */
    std::string domain;
    /**
     * @brief       SameSite 属性字符串，由调用者选择合法值。
     */
    std::string sameSite;
    /**
     * @brief       有效期秒数；负数表示不输出 Max-Age。
     */
    int maxAge{-1};
    /**
     * @brief       是否输出 Secure 属性。
     */
    bool secure{false};
    /**
     * @brief       是否输出 HttpOnly 属性。
     */
    bool httpOnly{false};
};
/**
 * @brief       解析请求 Cookie 头中的名称和值
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @param[in] header Cookie 请求头字段值，不包含字段名称
 * @return      独立持有名称和值的 Cookie 列表；无等号的片段被忽略。
 * @details     仅解析请求 Cookie 的名称和值，不解析 Set-Cookie 属性，不自动解码。
 */
UVHTTP_EXPORT std::vector<Cookie> parseCookies(std::string_view header);
/**
 * @brief       生成响应 Set-Cookie 字段值
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @param[in] cookie 需要序列化的 Cookie 属性
 * @return      Set-Cookie 字段值；Cookie 名为空时返回空字符串。
 * @details     不自动验证或转义名称、值及属性；调用者必须拒绝 CR/LF 等非法输入，防止头注入。
 */
UVHTTP_EXPORT std::string serializeCookie(const Cookie &cookie);
} // namespace uvhttp
#endif
