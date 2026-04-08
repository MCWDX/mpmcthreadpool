#pragma once

#include <vector>
#include <stdexcept>
#include <cassert>

namespace threadpool {

/**
 * 任务队列底层循环队列实现，其底层使用vector存储数据以提高缓存命中率。
 * 除了一般队列的访问队首元素功能外，还实现了随机访存队列其他元素的功能，通过重载[]运算符, 提供at()方法实现。
 * 
 * 队列实现了如下接口：
 * - 往队列添加元素相关方法：
 * - - pushBack()，
 * - - emplaceBack()，直接在队列中构建元素对象
 * - 访问队列元素相关方法：
 * - - front()
 * - - at()
 * - - 重载的[]运算符
 * - 获取队列状况相关方法：
 * - - empty(), 队列是否空
 * - - full()，队列是否满
 * - - size()，队列大小
 * - - capacity()，队列容量
 * - - overrunCount()，队列溢出计数器
 * - 重置队列状态相关方法：
 * - - clear()，清空队列且重置溢出计数器
 * - - resetCounter()，重置溢出计数器
 */
template <typename T>
class CircularQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    CircularQueue() : capacity_(0), elems_(0) {}
    /** @param capacity 循环队列所需容量 */ 
    explicit CircularQueue(size_type capacity) : capacity_(capacity > 0 ? capacity + 1 : 1), elems_(capacity_) {}
    
    ~CircularQueue() { clear(); };

    // 移动构造/赋值
    CircularQueue(CircularQueue<T>&& other);
    CircularQueue<T>& operator=(CircularQueue<T>&& other);

    // 添加操作
    void pushBack(T&& elem);
    void pushBack(const T& elem);
    template<typename... Args>
    void emplaceBack(Args&&... args);

    // 对队首操作
    void popFront();
    reference front();
    const_reference front() const;

    // 状态检查
    bool empty() const noexcept { return head_ == end_; };
    bool full() const noexcept { return capacity_ > 1 && ((end_ + 1) % capacity_) == head_; };  // 仅在队列容量大于0时判断队列是否已满
    size_type size() const noexcept;
    size_type capacity() const noexcept { return capacity_ > 1 ? capacity_ - 1 : 0; };
    size_type overrunCount() const noexcept { return overrun_count_; };

    // 访问元素
    reference at(size_type index);
    const_reference at(size_type index) const;
    reference operator[](size_type index) noexcept;
    const_reference operator[](size_type index) const noexcept;

    // 重置队列
    void clear() noexcept;
    void resetCounter() noexcept { overrun_count_ = 0; };

private:
    size_type capacity_{1};         // 队列实际容量，为所需容量+1
    size_type head_{0};             // 头指针
    size_type end_{0};              // 尾指针
    size_type overrun_count_{0};    // 溢出计数
    std::vector<T> elems_;          // 存储元素的容器
};

template<typename T>
CircularQueue<T>::CircularQueue(CircularQueue<T>&& other) 
    : capacity_(other.capacity_),
      head_(other.head_),
      end_(other.end_),
      overrun_count_(other.overrun_count_),
      elems_(std::move(other.elems_)) {
    other.capacity_ = 1;
    other.head_ = 0;
    other.end_ = 0;
}

template<typename T>
CircularQueue<T>& CircularQueue<T>::operator=(CircularQueue<T>&& other) {
    if (this != &other) {
        elems_ = std::move(other.elems_);
        head_ = other.head_;
        end_ = other.end_;
        overrun_count_ = other.overrun_count_;
        capacity_ = other.capacity_;

        other.capacity_ = 1;
        other.head_ = 0;
        other.end_ = 0;
    }
    return *this;
}

/**
 * @brief 往队列末尾移动元素，如果队列已满，则清理队首元素腾出位置
 */
template<typename T>
void CircularQueue<T>::pushBack(T&& elem) {
    if (capacity_ <= 1) {
        return;
    }

    elems_[end_] = std::move(elem);
    end_ = (end_ + 1) % capacity_;

    if (end_ == head_) {
        // 如果溢出
        head_ = (head_ + 1) % capacity_;
        overrun_count_++;
    }
}

/**
 * @brief 往队列末尾添加元素，如果队列已满，则清理队首元素腾出位置
 */
template<typename T>
void CircularQueue<T>::pushBack(const T& elem) {
    if (capacity_ <= 1) {
        return;
    }

    elems_[end_] = elem;
    end_ = (end_ + 1) % capacity_;

    if (end_ == head_) {
        // 如果溢出
        head_ = (head_ + 1) % capacity_;
        overrun_count_++;
    }
}

/**
 * @brief 往队列末尾构造元素，如果队列已满，则清理队首元素腾出位置
 */
template<typename T>
template <typename... Args>
void CircularQueue<T>::emplaceBack(Args&&... args) {
    if (capacity_ <= 1) {
        return;
    }

    elems_[end_] = T(std::forward<Args>(args)...);
    end_ = (end_ + 1) % capacity_;

    if (end_ == head_) {
        // 如果溢出
        head_ = (head_ + 1) % capacity_;
        overrun_count_++;
    }
}

/**
 * @brief 将队首元素出队
 */
template<typename T>
void CircularQueue<T>::popFront() {
    if (empty()) {
        throw std::runtime_error("Attempting pop when queue is empty");
    }
    // 出队的时候应该移动头指针，这不是栈
    head_ = (head_ + 1) % capacity_;
}

/**
 * @return 返回循环队列队首元素
 */
template<typename T>
typename CircularQueue<T>::reference CircularQueue<T>::front() {
    return const_cast<reference>(static_cast<const CircularQueue<T>&>(*this).front());
}

/**
 * @return 返回循环队列队首元素
 */
template<typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::front() const {
    if (empty()) {
        throw std::runtime_error("Attemping get front when queue is empty");
    }
    return elems_[head_];
}

/**
 * @return 返回目前循环队列元素个数
 */
template<typename T>
typename CircularQueue<T>::size_type CircularQueue<T>::size() const noexcept {
    if (head_ <= end_) {
        return end_ - head_;
    } else {
        return capacity_ - head_ + end_;
    }
}

/**
 * @brief 类似std::vector的中括号下标，访问队列其他位置元素
 * @param index 下标，从0开始
 * @return 返回队列第index个元素
 */
template<typename T>
typename CircularQueue<T>::reference CircularQueue<T>::at(size_type index) {
    return const_cast<reference>(static_cast<const CircularQueue<T>&>(*this).at(index));
}

/**
 * @brief 类似std::vector的at，访问队列其他位置元素
 * @param index 下标，从0开始
 * @return 返回队列第index个元素
 */
template<typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::at(size_type index) const {
    if (index >= size()) {
        throw std::out_of_range("Invalid index");
    }
    return elems_[(head_ + index) % capacity_];
}

/**
 * @brief 类似std::vector的at，访问队列其他位置元素
 * @param index 下标，从0开始
 * @return 返回队列第index个元素
 */
template<typename T>
typename CircularQueue<T>::reference CircularQueue<T>::operator[](size_type index) noexcept {
    return const_cast<reference>(static_cast<const CircularQueue<T>&>(*this)[index]);
}

/**
 * @brief 类似std::vector的中括号下标，访问队列其他位置元素
 * @param index 下标，从0开始
 * @return 返回队列第index个元素
 */
template<typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::operator[](size_type index) const noexcept {
    assert(index < size());
    return elems_[(head_ + index) % capacity_];
}

/**
 * @brief 清空循环队列
 */
template<typename T>
void CircularQueue<T>::clear() noexcept {
    head_ = end_ = 0;
    overrun_count_ = 0;
}

} // namespace threadpool