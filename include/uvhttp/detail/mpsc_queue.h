/**
 * @file        mpsc_queue.h
 * @brief       多生产者单消费者原子链式队列，供事件循环内部使用
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_DETAIL_MPSC_QUEUE_H
#define UVHTTP_DETAIL_MPSC_QUEUE_H

#include <atomic>
#include <optional>
#include <utility>

namespace uvhttp::detail
{

/**
 * @brief       多生产者单消费者原子链式队列，供事件循环内部使用。
 * @tparam      T 存储元素类型，须支持移动构造和移动赋值。
 */
template <typename T>
class MpscQueue
{
    struct Node
    {
        Node() = default;
        explicit Node(T value) : value_(std::move(value))
        {
        }
        std::atomic<Node *> next_{nullptr};
        std::optional<T> value_;
    };

  public:
    /**
     * @brief       构造 MpscQueue 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    MpscQueue() : head_(new Node), tail_(head_)
    {
    }
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    MpscQueue(const MpscQueue &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    MpscQueue &operator=(const MpscQueue &other) = delete;

    /**
     * @brief       释放 MpscQueue 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    ~MpscQueue()
    {
        Node *node = head_;
        while (node)
        {
            Node *next = node->next_.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

    /**
     * @brief       向 MPSC 队列发布一个元素
     * @threadsafe  push 可多生产者并发；tryPop/empty 仅限唯一消费者，析构前须停止所有访问。
     * @param[in] value 待存入或追加的值
     * @return      无。
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    void push(T value)
    {
        Node *node = new Node(std::move(value));
        Node *previous = tail_.exchange(node, std::memory_order_acq_rel);
        previous->next_.store(node, std::memory_order_release);
    }

    /**
     * @brief       由唯一消费者尝试取出一个元素
     * @threadsafe  push 可多生产者并发；tryPop/empty 仅限唯一消费者，析构前须停止所有访问。
     * @param[out] value 成功时接收弹出元素；失败时保持原值
     * @return      取到元素时 true；未见可消费节点时 false，不表示其他生产者已结束发布。
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    bool tryPop(T &value)
    {
        Node *next = head_->next_.load(std::memory_order_acquire);
        if (!next)
        {
            return false;
        }
        value = std::move(*next->value_);
        next->value_.reset();
        delete head_;
        head_ = next;
        return true;
    }

    /**
     * @brief       判断是否为空
     * @threadsafe  push 可多生产者并发；tryPop/empty 仅限唯一消费者，析构前须停止所有访问。
     * @return      true 表示所查询条件成立，false 表示不成立。
     * @details     使用原子链表发布节点，但分配仍可能加锁/抛异常，不承诺 wait-free 或无分配。
     */
    bool empty() const
    {
        return head_->next_.load(std::memory_order_acquire) == nullptr;
    }

  private:
    Node *head_;
    std::atomic<Node *> tail_;
};

} // namespace uvhttp::detail

#endif
