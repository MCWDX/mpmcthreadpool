#pragma once

#include "mpmc_blocking_queue.h"
#include "example_tasks.h"
#include "thread_pool_config.h"

#include <fmt/format.h>     // for formatter

namespace threadpool{

/**
 * 线程池类
 * 其中内嵌如下结构体与枚举类
 * - Status结构体，存储线程池状态
 * - State枚举类，表示线程池状态
 * - ShutdownOption枚举类，表示线程池关停选项
 * 
 * 线程池提供如下接口：
 * 提交任务相关方法：
 * - submit()，直接提交构建好的任务，以unique_ptr形式提交指向TaskBase子类的任务对象的智能指针
 * - submitWithResult()，提交待执行任务的函数，及其所需参数，返回一个std::future对象用于获取任务返回值
 * 
 * 等待线程池任务完成相关接口：
 * - waitForAllTask()，持续等待直至所有任务完成，过程中其他线程仍可提交任务需要调用者避免等待线程长时间等待
 * - waitForRunningTask(), 在给定时间内等待运行中的任务，如果给定时间内线程池完成所有任务返回true，否则返回false
 * 
 * 设置线程池状态相关接口：
 * - stop()，直接关停线程池
 * - shutdown()，根据关停选项关停线程池，能更好地处理各种情况的需求
 * - pause()，暂停线程池
 * - resume()，恢复线程池运行
 * 
 * 获取线程池状态相关接口：
 * - getState()，返回线程池当前状态
 * - isRunning()，判断是否运行中
 * - isPaused()，判断是否暂停
 * - isStopped()，判断是否关停
 * 
 * 任务队列状态相关接口：
 * - setQueueFullPolicy()，设置队满入队策略
 * - getQueueFullPolicy()，返回当前队满入队策略
 * - queueOverrunCount()，返回队列溢出计数
 * - queueDiscardCount()，返回队列丢弃计数
 * - taskCount()，返回当前任务数
 * 
 * 获取线程池信息相关接口
 * - threadCount()，当前线程数
 * - activeThreadCount()，活跃(执行任务中的)线程数
 * - coreThreadCount()，核心线程数，停用动态线程管理时工作线程数固定位核心线程数
 * - maxThreadCount()，最大线程数，停用动态线程管理时该值无意义
 * - getStatus()，返回当前线程池状态结构体
 * 
 * 动态线程管理相关接口
 * - loadCheck()立即调用一次载荷检查，可从线程池外部调用后提早触发动态扩容
 */
class ThreadPool {
public:
    using QueueType = MPMCBlockingQueue<std::unique_ptr<TaskBase>>;

    // 线程池状态结构体
    struct Status {
        friend std::ostream& operator<<(std::ostream&, const Status&);

        std::size_t tasks_succeeded{0};     // 完成任务数
        std::size_t tasks_failed{0};        // 失败任务数
        double avg_tasks_time{0.0};         // 任务平均耗时(毫秒)
        std::size_t active_threads{0};      // 活动线程数
        std::size_t peak_thread{0};         // 峰值线程数
        std::size_t thread_created{0};      // 线程创建数
        std::size_t thread_destroyed{0};    // 线程销毁数
        double load_factor{0.0};            // 载荷因子
    };
    
    // 线程池状态
    enum class State {
        RUNNING,        // 线程池运行中
        PAUSED,         // 线程池暂停
        SHUTING_DOWN,   // 线程池关闭中
        FORCE_SHUTING,  // 线程池强制关闭
        STOPPED         // 线程池已停止
    };

    static std::string stateToString(State state);

    // 线程池关停选项
    enum class ShutdownOption {
        WAIT_ALL,   // 等待完成所有任务再关
        FORCE,      // 强制立即关闭
        WAIT_FOR    // 等待一段时间后，还有任务也强制关闭
    };

    explicit ThreadPool(std::size_t thread_count, bool enable_dynamic_thread);

    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency(),
                        std::size_t max_queue_size = 1000,
                        QueueFullPolicy policy = QueueFullPolicy::BLOCKING,
                        bool enable_dynamic_thread = true);
    explicit ThreadPool(const Config& config);
    
    ~ThreadPool();

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator= (const ThreadPool&) = delete;

    // 提交任务
    bool submit(std::unique_ptr<TaskBase> task);
    template<typename F, typename... Args>
    auto submitWithResult(F&& func, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    // 等待线程池完成任务
    void waitForAllTask();
    bool waitForRunningTask(std::chrono::milliseconds wait_time);

    // 设置线程池状态
    void stop();
    void shutdown(ShutdownOption opt, std::chrono::milliseconds wait_time = std::chrono::milliseconds(1000));
    void pause();
    void resume();

    // 返回线程池状态
    State getState() const noexcept { return state_.load(); }
    bool isRunning() const noexcept { return state_.load() == State::RUNNING; }
    bool isPaused() const noexcept { return state_.load() == State::PAUSED;  };
    bool isStopped() const noexcept { return stopped_.load(); };

    // 任务队列相关函数
    void setQueueFullPolicy(QueueFullPolicy policy) noexcept { queue_policy_.store(policy); }
    QueueFullPolicy getQueueFullPolicy() const noexcept { return queue_policy_.load(); }
    std::size_t queueOverrunCount() { return task_queue_.overrunCount(); }
    std::size_t queueDiscardCount() const noexcept { return task_queue_.discardCount(); }
    std::size_t taskCount() { return task_queue_.size(); }

    // 获取线程池信息
    std::size_t threadCount() const noexcept { return thread_count_.load(); }
    std::size_t activeThreadCount() const noexcept { return active_threads_.load(); }
    std::size_t coreThreadCount() const noexcept { return config_.core_threads; }
    std::size_t maxThreadCount() const noexcept { return config_.max_threads; }
    Status getStatus() const;

    // 动态线程管理相关函数
    void loadCheck();

private:
    void workerLoop(std::size_t thread_index);

    // 动态管理线程相关函数
    void loadBalancerLoop();
    bool tryCreateNewThread();
    bool tryRemoveIdleThread();
    void cleanFinishedThread();
    double calLoadFactor() const;

    // 线程池状态私有函数
    void setState(State state);
    void forceStop();

    Config config_;

    // 工作线程相关变量
    std::vector<std::thread> workers_;              // 线程数组
    std::atomic<bool> stopped_{false};              // 线程池停止标志
    mutable std::mutex worker_access_mutex_;        // 互斥访问workers数组所用互斥量
    std::atomic<std::size_t> thread_count_{0};      // 当前线程数
    std::atomic<std::size_t> active_threads_{0};    // 运行中线程计数

    // 任务数相关变量
    std::atomic<std::size_t> pending_tasks_{0};     // 待处理任务数
    std::atomic<std::size_t> running_tasks_{0};     // 运行中的任务数

    // 线程信息相关变量
    std::vector<std::chrono::steady_clock::time_point> thread_last_active_;     // 线程最后活动时间
    std::vector<std::unique_ptr<std::atomic<std::size_t>>> thread_idle_count_;  // 线程空闲检查次数统计
    std::vector<std::unique_ptr<std::atomic<bool>>> thread_should_exit_;        // 线程是否应该退出

    // 载荷平衡线程相关变量
    std::thread load_balancer_;                     // 载荷线程本体
    std::atomic<bool> load_balancer_exit_{false};   // 载荷线程退出标志位

    // 任务队列及相关变量
    std::size_t max_queue_size_;                                            // 队列大小
    QueueType task_queue_;                                                  // 任务队列本体
    std::atomic<QueueFullPolicy> queue_policy_{QueueFullPolicy::BLOCKING};  // 队满时的入队策略
    mutable std::mutex queue_mutex_;                                        // 即使MPMCBlockingQueue内有锁设计，其size操作仍需额外互斥量控制
    std::condition_variable queue_cv_;                                      // 通知worker线程有任务的cv

    // 线程池状态相关变量
    std::atomic<State> state_{State::RUNNING};
    mutable std::mutex state_mutex_;
    std::condition_variable wait_cv_;

    // 线程池统计信息
    Status status_;                     // 统计信息
    mutable std::mutex status_mutex_;   // 统计信息访问互斥量
    std::condition_variable pause_cv_;  // 停止cv，用于停止线程池时供载荷平衡线程等待
};

/**
 * @brief 提交带返回值的任务，提交形式为提供可调用对象与其所需参数
 * @param func 可调用对象
 * @param args func所需参数
 * @return std::future实例，外部函数需要调用std::future.get()获取线程池返回的结果
 */
template<typename F, typename... Args>
auto ThreadPool::submitWithResult(F&& func, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    auto task = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
    
    using ReturnType = std::invoke_result_t<F, Args...>;
    auto task_ptr = std::make_unique<FutureTask<ReturnType>>(task);

    std::future<ReturnType> future = task_ptr->getFuture();
    bool submit_res = submit(std::move(task_ptr));

    if (!submit_res) {
        std::promise<ReturnType> promise;
        promise.set_exception(std::make_exception_ptr(std::future_error(std::future_errc::broken_promise)));
        return promise.get_future();
    }
    return future;
}

std::unique_ptr<ThreadPool> makeThreadPool(const std::string& file_path);
std::unique_ptr<ThreadPool> makeThreadPool(const Config& config);

} // namespace threadpool

namespace fmt{

/**
 * @brief 使得spdlog能接受threadpool::ThreadPool::Status类的特化类，特化了该类后就可以调用logger.info("{}", status)等方法
 */
template<>
struct formatter<threadpool::ThreadPool::Status> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const threadpool::ThreadPool::Status& status, FormatContext& ctx) const {
        return format_to(
            ctx.out(),
            "-----------Thread Pool Status-----------\n"
            "tasks succeeded: {}\n"
            "tasks failed: {}\n"
            "avg tasks time: {}\n"
            "active threads: {}\n"
            "peak thread: {}\n"
            "thread created: {}\n"
            "thread destroyed: {}\n"
            "load factor: {}\n"
            "----------------------------------------",
            status.tasks_succeeded,
            status.tasks_failed,
            status.avg_tasks_time,
            status.active_threads,
            status.peak_thread,
            status.thread_created,
            status.thread_destroyed,
            status.load_factor
        );
    }
};

} // namespace fmt