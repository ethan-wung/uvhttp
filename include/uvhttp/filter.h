/**
 * @file        filter.h
 * @brief       HTTP 过滤器链及同步调用栈内的后续处理接口
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_FILTER_H
#define UVHTTP_FILTER_H
#include <functional>
#include <vector>
#include "uvhttp/export.h"
#include "uvhttp/http.h"
namespace uvhttp
{
/**
 * @brief       HTTP 过滤器链及同步调用栈内的后续处理接口。
 */
class UVHTTP_EXPORT FilterChain
{
  public:
    /**
     * @brief       过滤器后续处理函数；仅在当前同步调用栈内调用一次，不得延迟保存。
     */
    using Next = std::function<void()>;
    /**
     * @brief       过滤器 (request, writer, Next)；可响应并中断链，或同步调用 Next 放行。
     */
    using Filter = std::function<void(const HttpRequest &, HttpResponseWriter, Next)>;
    /**
     * @brief       追加过滤器到链尾
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] filter 过滤器；Next 仅可在本次同步调用栈内调用，不能保存异步调用
     * @return      无。
     * @details     当前 Next 捕获调用栈内的执行状态，只能同步调用一次，不能存储到异步任务。
     */
    void add(Filter filter);
    /**
     * @brief       清空已注册过滤器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      无。
     * @details     当前 Next 捕获调用栈内的执行状态，只能同步调用一次，不能存储到异步任务。
     */
    void clear();
    /**
     * @brief       同步依次执行过滤器并在全部放行后调用终端处理器
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
     * @param[in] writer 当前请求的响应写入器，可按值保存
     * @param[in] terminal 所有过滤器放行后调用的终端处理函数
     * @return      无。
     * @details     当前 Next 捕获调用栈内的执行状态，只能同步调用一次，不能存储到异步任务。
     */
    void run(const HttpRequest &request, HttpResponseWriter writer, Next terminal) const;

  private:
    std::vector<Filter> filters_;
};
} // namespace uvhttp
#endif
