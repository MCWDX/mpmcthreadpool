#pragma once

#include <future>
#include <functional>

namespace threadpool {

/**
 * 任务基类，定义了如下虚函数
 * - execute()，纯虚函数，执行任务函数
 * - taskSuccess()，任务是否成功
 * - abandon()，丢弃任务
 */
class TaskBase {
public:
    virtual ~TaskBase() = default;

    virtual void execute() = 0;
    virtual bool taskSuccess() const { return true; }
    virtual void abandon() {}

protected:
    TaskBase() = default;

    // 禁止复制
    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    // 允许移动
    TaskBase(TaskBase&&) = default;
    TaskBase& operator=(TaskBase&&) = default;
};

/**
 * 带future返回值的任务类
 * 重写虚函数(execute(), taskSuccess(), abandon())，并添加了getFuture()函数用于获取任务返回值
 */
template<typename Ret>
class FutureTask : public TaskBase {
public:
    FutureTask(std::function<Ret()>&& func) : func_(std::move(func)) {}

    FutureTask(FutureTask<Ret>&& other);
    FutureTask& operator=(FutureTask<Ret>&& other);

    ~FutureTask() override = default;

    void execute() override;
    bool taskSuccess() const override { return task_success_; };
    void abandon() override;
    std::future<Ret> getFuture() { return promise_.get_future(); };

private:
    std::function<Ret()> func_;
    std::promise<Ret> promise_;
    bool task_success_{false};
};

template<typename Ret>
FutureTask<Ret>::FutureTask(FutureTask<Ret>&& other) 
    : func_(std::move(other.func_)),
      promise_(std::move(other.promise_)),
      task_success_(other.task_success_) {
}

template<typename Ret>
FutureTask<Ret>& FutureTask<Ret>::operator=(FutureTask<Ret>&& other) {
    if (this != &other) {
        func_ = std::move(other.func_);
        promise_ = std::move(other.promise_);
        task_success_ = other.task_success_;
    }
    return *this;
}

template<typename Ret>
void FutureTask<Ret>::execute() {
    try {
        if constexpr (std::is_void_v<Ret>) {
            std::invoke(func_);
            promise_.set_value();
        } else {
            promise_.set_value(std::invoke(func_));
        }
        task_success_ = true;
    } catch (...) {
        promise_.set_exception(std::current_exception());
        task_success_ = false;
    }
}

template<typename Ret>
void FutureTask<Ret>::abandon() {
    promise_.set_exception(std::make_exception_ptr(std::runtime_error("Task abandoned")));
    task_success_ = false;
}

/**
 * 简单增加任务，用于测试吞吐量
 */
class IncrementTask : public TaskBase {
public:
    explicit IncrementTask(std::atomic<long long>& counter) : counter_(counter) {}
    void execute() override { counter_.fetch_add(1, std::memory_order_relaxed); }
private:
    std::atomic<long long>& counter_;
};

} // namespace threadpool