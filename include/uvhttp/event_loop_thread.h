/**
 * @file        event_loop_thread.h
 * @brief       专用事件循环线程的启动、停止和等待接口
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_EVENT_LOOP_THREAD_H
#define UVHTTP_EVENT_LOOP_THREAD_H

#include <mutex>
#include <thread>

#include "uvhttp/event_loop.h"

namespace uvhttp
{

/**
 * @brief       专用事件循环线程的启动、停止和等待接口。
 */
class UVHTTP_EXPORT EventLoopThread
{
  public:
    /**
     * @brief       构造 EventLoopThread 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     */
    EventLoopThread() = default;
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    EventLoopThread(const EventLoopThread &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    EventLoopThread &operator=(const EventLoopThread &other) = delete;
    /**
     * @brief       释放 EventLoopThread 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     析构请求 stop 并等待线程退出；不能在自身工作线程析构，所有外部句柄须先关闭。
     */
    ~EventLoopThread();

    /**
     * @brief       创建专用工作线程并返回其事件循环
     * @threadsafe  有限；启动/停止/等待由同一控制线程串行管理，不得在被等待线程调用 join。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     * @details     同一对象只能启动一次；重复启动抛 std::logic_error。返回引用在工作线程退出后失效。
     */
    EventLoop &start();
    /**
     * @brief       异步请求停止
     * @threadsafe  有限；启动/停止/等待由同一控制线程串行管理，不得在被等待线程调用 join。
     * @return      无。
     */
    void stop() noexcept;
    /**
     * @brief       等待工作线程退出
     * @threadsafe  有限；启动/停止/等待由同一控制线程串行管理，不得在被等待线程调用 join。
     * @return      无。
     * @details     不隐式请求 stop；调用前安排关闭，否则可能一直等待。不能等待当前线程自身。
     */
    void join();

  private:
    std::mutex mutex_;
    std::thread thread_;
    EventLoop *loop_{nullptr};
    bool started_{false};
};

} // namespace uvhttp

#endif
