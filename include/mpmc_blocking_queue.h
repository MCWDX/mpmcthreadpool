#pragma once

#include "circular_queue.h"

#include <mutex>
#include <atomic>
#include <condition_variable>

namespace threadpool {

/**
 * 多生产者多消费者阻塞队列，用于多线程环境安全地处理队列操作
 * MPMC阻塞队列实现了如下接口：
 * - 入队方法：
 * - - enqueue()，普通阻塞入队
 * - - enqueueNoWait()，立即将新元素入队，如果队列已满则覆盖最旧的内容，并记录溢出计数器
 * - - enqueueIfNotFull()，如果队非满则入队，否则丢弃新元素，并记录丢弃计数器
 * - 出队方法：
 * - - dequeue()，阻塞出队
 * - - dequeueWaitFor()，一段时间内尝试出队，可避免无限等待
 * - 获取队列状态相关方法：
 * - - size()，队列大小
 * - - capacity()，队列容量
 * - - overrunCount()，溢出计数器
 * - - discardCount()，丢弃计数器
 */
template<typename T>
class MPMCBlockingQueue {
public:
    MPMCBlockingQueue() = delete;
    explicit MPMCBlockingQueue(std::size_t max_elems) : discard_count_(0), circular_queue_(max_elems) {}
    ~MPMCBlockingQueue() = default;

    void enqueue(T&& elem);
    void enqueueNoWait(T&& elem);
    bool enqueueIfNotFull(T&& elem);

    void dequeue(T& pop_res);
    bool dequeueWaitFor(T& pop_res, std::chrono::milliseconds wait_time);

    std::size_t size();
    std::size_t capacity();
    std::size_t overrunCount();
    std::size_t discardCount() const { return discard_count_.load(std::memory_order::memory_order_relaxed); };
private:
    std::mutex queue_mutex_;                        // 互斥锁所需互斥量
    std::condition_variable push_cv_;               // 入队cv
    std::condition_variable pop_cv_;                // 出队cv
    std::atomic<std::size_t> discard_count_{0};     // 丢弃计数
    CircularQueue<T> circular_queue_;               // 底层循环队列存储元素
};

/**
 * @brief 阻塞入队，当队列满的时候阻塞等待直至队列有空位
 * @param elem 待入队元素
 */
template<typename T>
void MPMCBlockingQueue<T>::enqueue(T&& elem) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        pop_cv_.wait(lock, [this]() { return !circular_queue_.full(); });

        circular_queue_.pushBack(std::move(elem));
    }
    push_cv_.notify_one();
}

/**
 * @brief 立即入队，无视当前队列是否满，直接入队
 * @param elem 待入队元素
 */
template<typename T>
void MPMCBlockingQueue<T>::enqueueNoWait(T&& elem) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 无wait
        circular_queue_.pushBack(std::forward<T>(elem));
    }
    push_cv_.notify_one();
}

/**
 * @brief 如果队列没满，则将元素入队，如果队列已满，不入队立即返回结果
 * @param elem 入队元素
 * @return 是否成功入队
 */
template<typename T>
bool MPMCBlockingQueue<T>::enqueueIfNotFull(T&& elem) {
    bool pushed = false;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (!circular_queue_.full()) {
            circular_queue_.pushBack(std::forward<T>(elem));
            pushed = true;
        }
    }
    if (pushed) {
        push_cv_.notify_one();
    } else {
        // 如果队列已满无法入队，丢弃计数器自增1
        discard_count_.fetch_add(1LL, std::memory_order::memory_order_relaxed);
    }
    return pushed;
}

/**
 * @brief 阻塞出队，并将出队元素写入pop_res，会阻塞等待直至有元素出队
 * @param pop_res 用于存放出队元素
 */
template<typename T>
void MPMCBlockingQueue<T>::dequeue(T& pop_res) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 如果空则等待元素入队
        push_cv_.wait(lock, [this]() { return !circular_queue_.empty(); });
        
        pop_res = std::move(circular_queue_.front());
        circular_queue_.popFront();
    }
    pop_cv_.notify_one();
}

/**
 * @brief 阻塞等待出队，最多等待wait_time毫秒，并根据是否成功出队元素返回布尔值，如果有元素出队则写入pop_res
 * @param pop_res 存储出队元素
 * @param wait_time 等待时长，单位毫秒
 * @return 是否有元素出队
 */
template<typename T>
bool MPMCBlockingQueue<T>::dequeueWaitFor(T& pop_res, std::chrono::milliseconds wait_time) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 空的时候才需要等待
        if (!push_cv_.wait_for(lock, wait_time, [this]() { return !circular_queue_.empty(); })) {
            return false;
        }

        pop_res = std::move(circular_queue_.front());
        circular_queue_.popFront();
        
    }
    pop_cv_.notify_one();
    return true;
}

/**
 * @return 返回当前队列大小(包含元素个数)
 */
template<typename T>
std::size_t MPMCBlockingQueue<T>::size() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return circular_queue_.size();
}

/**
 * @return 返回队列容量
 */
template<typename T>
std::size_t MPMCBlockingQueue<T>::capacity() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return circular_queue_.capacity();
}

/**
 * @return 返回队列溢出计数
 */
template<typename T>
std::size_t MPMCBlockingQueue<T>::overrunCount() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return circular_queue_.overrunCount();
}

} // namespace threadpool