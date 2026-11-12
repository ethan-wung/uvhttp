/**
 * @file        event_loop_thread.cc
 * @brief       专用事件循环线程的启动、停止和等待接口的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/event_loop_thread.h"

#include <future>
#include <memory>
#include <stdexcept>
#include <utility>

namespace uvhttp
{

EventLoopThread::~EventLoopThread()
{
    stop();
    if (thread_.joinable())
    {
        if (thread_.get_id() == std::this_thread::get_id())
        {
            std::terminate();
        }
        thread_.join();
    }
}

EventLoop &EventLoopThread::start()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_)
        {
            throw std::logic_error("EventLoopThread can only be started once");
        }
        started_ = true;
    }
    std::promise<EventLoop *> readyPromise;
    std::future<EventLoop *> readyFuture = readyPromise.get_future();
    thread_ = std::thread([this, promise = std::move(readyPromise)]() mutable {
        std::unique_ptr<EventLoop> loop;
        try
        {
            loop = std::make_unique<EventLoop>();
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
            return;
        }
        EventLoop *loopPointer = loop.get();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = loopPointer;
        }
        promise.set_value(loopPointer);
        loop->run();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = nullptr;
        }
    });
    try
    {
        return *readyFuture.get();
    }
    catch (...)
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        throw;
    }
}
void EventLoopThread::stop() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (loop_)
    {
        loop_->stop();
    }
}

void EventLoopThread::join()
{
    if (thread_.joinable())
    {
        if (thread_.get_id() == std::this_thread::get_id())
        {
            throw std::logic_error("EventLoopThread cannot join itself");
        }
        thread_.join();
    }
}

}  // namespace uvhttp
