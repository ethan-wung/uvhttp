/**
 * @file        event_loop.cc
 * @brief       基于 libuv 的事件循环、跨线程任务和定时器的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/event_loop.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace uvhttp
{
struct EventLoop::Timer
{
    uv_timer_t handle{};
    EventLoop *owner;
    TimerId id;
    Task task;
    static void closed(uv_handle_t *handle) noexcept
    {
        delete static_cast<Timer *>(handle->data);
    }
};

EventLoop::TimerId EventLoop::scheduleAfter(std::chrono::milliseconds delay, Task task)
{
    if (!isLoopThread() || !isRunning() || !task || delay.count() < 0) return 0;
    auto *timer = new Timer{{}, this, nextTimerId_++, std::move(task)};
    if (uv_timer_init(&loop_, &timer->handle) != 0) { delete timer; return 0; }
    timer->handle.data = timer;
    try { timers_.emplace(timer->id, timer); }
    catch (...) { uv_close(reinterpret_cast<uv_handle_t *>(&timer->handle), Timer::closed); throw; }
    const int result = uv_timer_start(&timer->handle, [](uv_timer_t *handle) {
        auto *timer = static_cast<Timer *>(handle->data);
        auto *owner = timer->owner;
        auto task = std::move(timer->task);
        owner->cancelTimer(timer->id);
        if (!owner->isRunning()) return;
        try { task(); } catch (...) { owner->reportException(std::current_exception()); }
    }, static_cast<std::uint64_t>(delay.count()), 0);
    if (result != 0) { cancelTimer(timer->id); return 0; }
    return timer->id;
}

bool EventLoop::cancelTimer(TimerId id) noexcept
{
    if (!isLoopThread()) return false;
    auto found = timers_.find(id);
    if (found == timers_.end()) return false;
    auto *timer = found->second;
    timers_.erase(found);
    uv_timer_stop(&timer->handle);
    timer->task = {};
    uv_close(reinterpret_cast<uv_handle_t *>(&timer->handle), Timer::closed);
    return true;
}

void EventLoop::closeTimers() noexcept
{
    while (!timers_.empty()) cancelTimer(timers_.begin()->first);
}

EventLoop::TimerId EventLoop::addShutdownHook(Task task)
{
    if (!isLoopThread() || !isRunning() || !task) return 0;
    const auto id = nextTimerId_++;
    shutdownHooks_.emplace(id, std::move(task));
    return id;
}

bool EventLoop::removeShutdownHook(TimerId id) noexcept
{
    return isLoopThread() && shutdownHooks_.erase(id) != 0;
}

EventLoop::EventLoop() : ownerThread_(std::this_thread::get_id())
{
    int status = uv_loop_init(&loop_);
    if (status < 0)
    {
        throw std::runtime_error(uv_strerror(status));
    }
    status = uv_async_init(&loop_, &async_, &EventLoop::onAsync);
    if (status < 0)
    {
        uv_loop_close(&loop_);
        throw std::runtime_error(uv_strerror(status));
    }
    async_.data = this;
}

EventLoop::~EventLoop()
{
    if (!isLoopThread())
    {
        std::terminate();
    }
    if (!runCalled_.load(std::memory_order_acquire))
    {
        stop();
        run();
    }
    if (uv_loop_close(&loop_) != 0)
    {
        std::terminate();
    }
}

void EventLoop::run()
{
    if (!isLoopThread())
    {
        throw std::logic_error("EventLoop::run must be called on its creating thread");
    }
    bool expected = false;
    if (!runCalled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        throw std::logic_error("EventLoop::run may only be called once");
    }
    uv_run(&loop_, UV_RUN_DEFAULT);
    if (state_.load(std::memory_order_acquire) != State::Stopped)
    {
        std::terminate();
    }
}

bool EventLoop::post(Task task) noexcept
{
    if (!task)
    {
        return false;
    }
    activePosters_.fetch_add(1, std::memory_order_seq_cst);
    if (state_.load(std::memory_order_seq_cst) != State::Running)
    {
        activePosters_.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    try
    {
        tasks_.push(std::move(task));
    }
    catch (...)
    {
        activePosters_.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    int status = uv_async_send(&async_);
    activePosters_.fetch_sub(1, std::memory_order_seq_cst);
    return status == 0;
}

bool EventLoop::stop() noexcept
{
    State expected = State::Running;
    if (!state_.compare_exchange_strong(expected,
                                        State::Stopping,
                                        std::memory_order_seq_cst))
    {
        return false;
    }
    return uv_async_send(&async_) == 0;
}

bool EventLoop::isLoopThread() const noexcept
{
    return std::this_thread::get_id() == ownerThread_;
}

bool EventLoop::isRunning() const noexcept
{
    return state_.load(std::memory_order_acquire) == State::Running;
}

void EventLoop::setExceptionHandler(ExceptionHandler handler)
{
    std::lock_guard<std::mutex> lock(exceptionMutex_);
    exceptionHandler_ = std::move(handler);
}

void EventLoop::onAsync(uv_async_t *handle) noexcept
{
    auto *loop = static_cast<EventLoop *>(handle->data);
    loop->drainTasks();
}

void EventLoop::onAsyncClosed(uv_handle_t *handle) noexcept
{
    auto *loop = static_cast<EventLoop *>(handle->data);
    loop->state_.store(State::Stopped, std::memory_order_release);
}

void EventLoop::drainTasks() noexcept
{
    Task task;
    std::size_t handled = 0;
    while (handled < 256 && tasks_.tryPop(task))
    {
        try
        {
            task();
        }
        catch (...)
        {
            reportException(std::current_exception());
        }
        task = {};
        ++handled;
    }

    if (!tasks_.empty())
    {
        uv_async_send(&async_);
        return;
    }

    if (state_.load(std::memory_order_seq_cst) == State::Stopping)
    {
        if (activePosters_.load(std::memory_order_seq_cst) != 0)
        {
            uv_async_send(&async_);
            return;
        }
        State expected = State::Stopping;
        if (state_.compare_exchange_strong(expected,
                                           State::Closing,
                                           std::memory_order_seq_cst))
        {
            closeTimers();
            auto hooks = std::move(shutdownHooks_);
            for (auto &entry : hooks)
            {
                try { entry.second(); }
                catch (...) { reportException(std::current_exception()); }
            }
            uv_close(reinterpret_cast<uv_handle_t *>(&async_), &EventLoop::onAsyncClosed);
        }
    }
}

void EventLoop::reportException(std::exception_ptr exception) noexcept
{
    ExceptionHandler handler;
    try
    {
        std::lock_guard<std::mutex> lock(exceptionMutex_);
        handler = exceptionHandler_;
    }
    catch (...)
    {
        std::terminate();
    }
    if (!handler)
    {
        std::terminate();
    }
    try
    {
        handler(exception);
    }
    catch (...)
    {
        std::terminate();
    }
}

}  // namespace uvhttp
