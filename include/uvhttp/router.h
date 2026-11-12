/**
 * @file        router.h
 * @brief       按方法和路径模式匹配的 HTTP 路由器
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_ROUTER_H
#define UVHTTP_ROUTER_H

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "uvhttp/export.h"
#include "uvhttp/http.h"

namespace uvhttp
{
/**
 * @brief       路由匹配输出，包含命名路径参数。
 */
struct UVHTTP_EXPORT RouteMatch
{
    /**
     * @brief       匹配到的命名路径参数；其内容未承诺自动 URL 解码。
     */
    std::unordered_map<std::string, std::string> parameters;
};
/**
 * @brief       按方法和路径模式匹配的 HTTP 路由器。
 */
class UVHTTP_EXPORT Router
{
  public:
    /**
     * @brief       路由处理函数 (request, RouteMatch, writer)，引用仅在调用期间有效。
     */
    using Handler =
        std::function<void(const HttpRequest &, const RouteMatch &, HttpResponseWriter)>;
    /**
     * @brief       注册 HTTP 方法与路径模式处理器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] method HTTP 方法；路由注册可用 * 匹配任意方法
     * @param[in] pattern 路由模式，支持 :name 参数及末尾 ** 通配
     * @param[in] handler 用户处理函数
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool add(std::string method, std::string pattern, Handler handler);
    /**
     * @brief       删除第一个匹配方法和路径模式的路由
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] method HTTP 方法；路由注册可用 * 匹配任意方法
     * @param[in] pattern 路由模式，支持 :name 参数及末尾 ** 通配
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool remove(std::string_view method, std::string_view pattern);
    /**
     * @brief       按请求方法和路径分发到路由处理器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] writer 当前请求的响应写入器，可按值保存
     * @return      匹配到路由时为 true；未匹配时 false，不自动生成 404。
     */
    bool dispatch(const HttpRequest &request, HttpResponseWriter writer) const;
    /**
     * @brief       匹配路由并输出路径参数和处理器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] method HTTP 方法；路由注册可用 * 匹配任意方法
     * @param[in] path 待匹配路径，匹配时忽略查询串
     * @param[out] result 匹配结果及命名路径参数
     * @param[out] handler 可选处理器输出；不为空时在匹配成功后赋值
     * @return      匹配到路由时为 true；未匹配时 false，不自动生成 404。
     */
    bool match(std::string_view method, std::string_view path, RouteMatch &result,
               Handler *handler = nullptr) const;
    /**
     * @brief       清空已注册路由
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      无。
     */
    void clear();

  private:
    struct Route
    {
        std::string method;
        std::string pattern;
        Handler handler;
    };
    std::vector<Route> routes_;
};
} // namespace uvhttp
#endif
