/**
 * @file        event_loop.h
 * @brief       基于 libuv 的事件循环、跨线程任务和定时器
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_EVENT_LOOP_H
#define UVHTTP_EVENT_LOOP_H

#include <atomic>

#include "uvhttp/export.h"
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include <uv.h>

#include "uvhttp/detail/mpsc_queue.h"

namespace uvhttp
{

/**
 * @brief       基于 libuv 的事件循环、跨线程任务和定时器。
 */
class UVHTTP_EXPORT EventLoop
{
  public:
    /**
     * @brief       无参数任务函数；事件循环任务不得阻塞或抛出未处理异常。
     */
    using Task = std::function<void()>;
    /**
     * @brief       接收 exception_ptr 的异常处理函数。
     */
    using ExceptionHandler = std::function<void(std::exception_ptr)>;
    /**
     * @brief       单个事件循环内的定时器/关闭 hook 标识，0 表示注册失败。
     */
    using TimerId = std::uint64_t;

    /**
     * @brief       构造 EventLoop 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     */
    EventLoop();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    EventLoop(const EventLoop &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    EventLoop &operator=(const EventLoop &other) = delete;
    /**
     * @brief       释放 EventLoop 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     必须在创建线程析构；未调用 run 时会请求停止并运行关闭流程。未关闭句柄或错误线程析构会终止进程。
     */
    ~EventLoop();

    /**
     * @brief       运行事件循环
     * @threadsafe  否；必须在创建该事件循环的线程调用。
     * @return      无。
     * @details     只能在创建线程调用一次；run 返回后仍须确保所有 libuv 句柄完成关闭再析构。
     */
    void run();
    /**
     * @brief       向所属事件循环投递任务
     * @threadsafe  是；对象生命周期须由调用者保证。
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool post(Task task) noexcept;
    /**
     * @brief       注册单次延时任务
     * @threadsafe  否；必须在创建该事件循环的线程调用。
     * @param[in] delay 非负等待时间，实际执行取决于事件循环调度
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      注册成功返回非零标识，失败返回 0。
     */
    TimerId scheduleAfter(std::chrono::milliseconds delay, Task task);
    /**
     * @brief       取消尚未执行的定时任务
     * @threadsafe  否；必须在创建该事件循环的线程调用。
     * @param[in] id 本 loop 返回的定时器或关闭 hook 标识
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool cancelTimer(TimerId id) noexcept;
    /**
     * @brief       注册事件循环关闭时执行的任务
     * @threadsafe  否；必须在创建该事件循环的线程调用。
     * @param[in] task 待执行任务；不得捕获在任务执行前失效的引用
     * @return      注册成功返回非零标识，失败返回 0。
     */
    TimerId addShutdownHook(Task task);
    /**
     * @brief       移除关闭任务
     * @threadsafe  否；必须在创建该事件循环的线程调用。
     * @param[in] id 本 loop 返回的定时器或关闭 hook 标识
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool removeShutdownHook(TimerId id) noexcept;
    /**
     * @brief       异步请求停止
     * @threadsafe  是；对象生命周期须由调用者保证。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     请求停止任务处理并运行关闭 hook，不替代手动关闭全部外部监听器/连接。
     */
    bool stop() noexcept;
    /**
     * @brief       判断调用线程是否为所属事件循环线程
     * @threadsafe  是；对象生命周期须由调用者保证。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isLoopThread() const noexcept;
    /**
     * @brief       查询事件循环是否仍接受工作
     * @threadsafe  是；对象生命周期须由调用者保证。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isRunning() const noexcept;
    /**
     * @brief       设置未捕获任务异常的处理回调
     * @threadsafe  是；对象生命周期须由调用者保证。
     * @param[in] handler 异常处理函数；不应再抛异常，空回调恢复默认异常终止行为
     * @return      无。
     */
    void setExceptionHandler(ExceptionHandler handler);

  private:
    enum class State : unsigned char
    {
        Running,
        /**
         * @brief       正在关闭监听器及活动连接。
         */
        Stopping,
        Closing,
        /**
         * @brief       监听器及活动连接已停止。
         */
        Stopped
    };

    static void onAsync(uv_async_t *handle) noexcept;
    static void onAsyncClosed(uv_handle_t *handle) noexcept;
    void drainTasks() noexcept;
    void reportException(std::exception_ptr exception) noexcept;

    uv_loop_t loop_{};
    uv_async_t async_{};
    detail::MpscQueue<Task> tasks_;
    std::atomic<State> state_{State::Running};
    std::atomic<unsigned int> activePosters_{0};
    std::atomic<bool> runCalled_{false};
    std::thread::id ownerThread_;
    std::mutex exceptionMutex_;
    ExceptionHandler exceptionHandler_;
    struct Timer;
    TimerId nextTimerId_{1};
    std::unordered_map<TimerId, Timer *> timers_;
    std::unordered_map<TimerId, Task> shutdownHooks_;
    void closeTimers() noexcept;

    friend class TcpConnection;
    friend class TcpClient;
    friend class TcpServer;
};

} // namespace uvhttp

#endif
