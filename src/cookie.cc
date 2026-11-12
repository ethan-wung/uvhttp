/**
 * @file        cookie.cc
 * @brief       Cookie 数据结构、请求 Cookie 解析与响应序列化的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/cookie.h"
#include <cctype>
namespace uvhttp { namespace { std::string trim(std::string_view s) { while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1); while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1); return std::string(s); } }
std::vector<Cookie> parseCookies(std::string_view header) { std::vector<Cookie> out; std::size_t pos=0; while (pos<header.size()) { auto end=header.find(';',pos); auto item=header.substr(pos,end==std::string_view::npos?header.size()-pos:end-pos); auto eq=item.find('='); if (eq!=std::string_view::npos) out.push_back({trim(item.substr(0,eq)),trim(item.substr(eq+1))}); if(end==std::string_view::npos) break; pos=end+1; } return out; }
std::string serializeCookie(const Cookie &c) { if(c.name.empty()) return {}; std::string s=c.name+"="+c.value; if(!c.path.empty()) s+="; Path="+c.path; if(!c.domain.empty()) s+="; Domain="+c.domain; if(c.maxAge>=0) s+="; Max-Age="+std::to_string(c.maxAge); if(c.secure)s+="; Secure"; if(c.httpOnly)s+="; HttpOnly"; if(!c.sameSite.empty())s+="; SameSite="+c.sameSite; return s; }
}
