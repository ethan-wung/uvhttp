/**
 * @file        application.h
 * @brief       HTTP 应用入口，组合路由、过滤器与会话存储
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_APPLICATION_H
#define UVHTTP_APPLICATION_H
#include "uvhttp/export.h"
#include "uvhttp/filter.h"
#include "uvhttp/http_server.h"
#include "uvhttp/router.h"
#include "uvhttp/session.h"
namespace uvhttp
{
/**
 * @brief       HTTP 应用入口，组合路由、过滤器与会话存储。
 */
class UVHTTP_EXPORT HttpApplication
{
  public:
    /**
     * @brief       构造 HttpApplication 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     */
    HttpApplication() = default;
    /**
     * @brief       取得应用路由器引用
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     */
    Router &router() noexcept
    {
        return router_;
    }
    /**
     * @brief       取得应用过滤器链引用
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     */
    FilterChain &filters() noexcept
    {
        return filters_;
    }
    /**
     * @brief       取得应用会话存储引用
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     */
    SessionStore &sessions() noexcept
    {
        return sessions_;
    }
    /**
     * @brief       创建供 HttpServer 使用的请求处理回调
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      回调集合，捕获当前应用对象，应用须活到 server 停止。
     */
    HttpServer::Callbacks callbacks();
    /**
     * @brief       按请求方法和路径分发到路由处理器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] writer 当前请求的响应写入器，可按值保存
     * @return      路由已匹配并调用 handler 时 true，否则 false；不自动执行过滤器或发送 404。
     * @details     此方法仅分发路由，不自动执行过滤器；需要完整应用处理时使用 callbacks。
     */
    bool dispatch(const HttpRequest &request, HttpResponseWriter writer) const;

  private:
    Router router_;
    FilterChain filters_;
    SessionStore sessions_;
};
} // namespace uvhttp
#endif
