#include "thread_pool.h"
#include "logger.h"

#include <iostream>
#include <thread>

using std::unique_lock;
using std::mutex;
using std::chrono::milliseconds;
namespace chrono = std::chrono;

namespace threadpool{
/**
 * @brief 重载<<运算符使得可以直接cout << ThreadPool::Stat
 */
std::ostream& operator<<(std::ostream& os, const ThreadPool::Status& status) {
    os << "-----------Thread Pool Status-----------" << std::endl;
    os << "tasks succeeded: " << status.tasks_succeeded << std::endl;
    os << "tasks failed: " << status.tasks_failed << std::endl;
    os << "avg tasks time: " << status.avg_tasks_time << std::endl;
    os << "active threads: " << status.active_threads << std::endl;
    os << "peak thread: " << status.peak_thread << std::endl;
    os << "thread created: " << status.thread_created << std::endl;
    os << "thread destroyed: " << status.thread_destroyed << std::endl;
    os << "load factor: " << status.load_factor << std::endl;
    os << "----------------------------------------" << std::endl;
    return os;
}

std::string ThreadPool::stateToString(State state) {
    switch (state) {
        case State::RUNNING:
            return "running";
        case State::PAUSED:
            return "paused";
        case State::SHUTING_DOWN:
            return "shutting down";
        case State::FORCE_SHUTING:
            return "force shutting";
        case State::STOPPED:
            return "stopped";
        default:
            return "Unknown state";
    }
}

/**
 * @param thread_count 线程池最大线程数
 * @param enable_dynamic_thread 是否启用动态线程
 */
ThreadPool::ThreadPool(std::size_t thread_count, bool enable_dynamic_thread) : 
    ThreadPool(thread_count, 1000, QueueFullPolicy::BLOCKING, enable_dynamic_thread) {}

/**
 * @param thread_count 线程池最大线程数
 * @param max_queue_size 任务队列大小
 * @param policy 队满入队策略
 * @param enable_dynamic_thread 是否启用动态线程
 */
ThreadPool::ThreadPool(std::size_t thread_count,
                       std::size_t max_queue_size,
                       QueueFullPolicy policy,
                       bool enable_dynamic_thread)
    : max_queue_size_(max_queue_size),
      task_queue_(max_queue_size),
      queue_policy_(policy){
    config_.max_threads = thread_count >= config_.core_threads ? thread_count : config_.core_threads;
    config_.enable_dynamic_thread = enable_dynamic_thread;
    for (std::size_t i = 0; i < config_.max_threads; i++) {
        thread_last_active_.emplace_back(chrono::steady_clock::now());
        thread_idle_count_.emplace_back(std::make_unique<std::atomic<std::size_t>>(0));
        thread_should_exit_.emplace_back(std::make_unique<std::atomic<bool>>(false));
    }
    workers_.reserve(config_.max_threads);
    if (config_.enable_dynamic_thread) {
        load_balancer_ = std::thread(&ThreadPool::loadBalancerLoop, this);
    }
    for (std::size_t i = 0; i < config_.core_threads; i++) {
        {
            unique_lock<mutex> worker_lock(worker_access_mutex_);
            workers_.emplace_back(&ThreadPool::workerLoop, this, i);
        }
        {
            unique_lock<mutex> status_lock(status_mutex_);
            status_.thread_created++;
            thread_count_++;
            std::size_t thread_count = thread_count_.load();
            if (thread_count > status_.peak_thread) {
                status_.peak_thread = thread_count;
            }
        }
    }
    
}

/**
 * @param config threadpool::Config结构体，存放线程池配置信息
 */
ThreadPool::ThreadPool(const Config& config) 
    : config_(config),
      task_queue_(config.max_queue_size),
      queue_policy_(config.policy) {
    for (std::size_t i = 0; i < config_.max_threads; i++) {
        thread_last_active_.emplace_back(chrono::steady_clock::now());
        thread_idle_count_.emplace_back(std::make_unique<std::atomic<std::size_t>>(0));
        thread_should_exit_.emplace_back(std::make_unique<std::atomic<bool>>(false));
    }
    workers_.reserve(config_.max_threads);
    if (config_.enable_dynamic_thread) {
        load_balancer_ = std::thread(&ThreadPool::loadBalancerLoop, this);
    }
    for (std::size_t i = 0; i < config_.core_threads; i++) {
        {
            unique_lock<mutex> worker_lock(worker_access_mutex_);
            workers_.emplace_back(&ThreadPool::workerLoop, this, i);
        }
        {
            unique_lock<mutex> status_lock(status_mutex_);
            status_.thread_created++;
            thread_count_++;
            std::size_t thread_count = thread_count_.load();
            if (thread_count > status_.peak_thread) {
                status_.peak_thread = thread_count;
            }
        }
    }
}

/**
 * @brief 析构函数默认使用WAIT_FOR等待1秒后再关停线程池
 */
ThreadPool::~ThreadPool() {
    shutdown(ShutdownOption::WAIT_FOR, milliseconds(1000));
}

/**
 * @brief 往线程池提交包装好的任务
 * @param task 指向TaskBase派生类的unique_ptr，将任务包装好之后再传给这个函数入队
 * @return 任务是否成功入队，在BLOCKING与OVERWRITE策略下一定能成功入队，在DISCARD策略下可能入队失败
 */
bool ThreadPool::submit(std::unique_ptr<TaskBase> task) {
    bool submit_res = false;
    if (!isRunning()) {
        throw std::runtime_error("Attempting to submit task when thread pool can't receive task");
    }
    switch (queue_policy_.load()) {
        case QueueFullPolicy::BLOCKING:
            task_queue_.enqueue(std::move(task));
            submit_res = true;
            break;
        case QueueFullPolicy::OVERWRITE:
            task_queue_.enqueueNoWait(std::move(task));
            submit_res = true;
            break;
        case QueueFullPolicy::DISCARD:
            submit_res = task_queue_.enqueueIfNotFull(std::move(task));
            break;
    }
    
    if (submit_res) {
        // 提交任务之后要提醒worker线程来干活了
        pending_tasks_++;
        unique_lock<mutex> queue_lock(queue_mutex_);
        queue_cv_.notify_one();
    }
    return submit_res;
}

/**
 * @brief 等待所有任务结束后再继续下一步，作为公有函数，其缺陷是线程A等待期间其他线程仍可提交任务，可能导致线程A饥饿
 * @brief 建议调用waitForRunningTask确保等待线程不会饥饿
 * @brief 如确实需要调用该函数，由调用者保证其他函数不会频繁提交任务或提交长时任务
 */
void ThreadPool::waitForAllTask() {
    unique_lock<mutex> queue_lock(queue_mutex_);
    wait_cv_.wait(queue_lock, [this]() {
        // 或者也可以active_threads_.load() == 0 && task_queue_.size() == 0;
        return pending_tasks_.load() == 0 && running_tasks_.load() == 0;
    });
}

/**
 * @brief 有限等待任务函数，相较于waitForAllTask的优点是不会导致线程饥饿
 * @param wait_time 等待时间, 单位毫秒
 * @return 返回时线程池是否空闲无任务
 */
bool ThreadPool::waitForRunningTask(milliseconds wait_time) {
    auto begin = chrono::steady_clock::now();
    while (true) {
        std::size_t running = running_tasks_.load();
        std::size_t pending = pending_tasks_.load();
        if (running == 0 && pending == 0) {
            return true;
        }

        auto waited = chrono::steady_clock::now() - begin;
        if (waited >= wait_time) {
            LOG_WARN(
                "Wait for running task time out, there were {} tasks running, {} tasks pending",
                running,
                pending
            );
            return false;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
}

/**
 * @brief 停止线程池，join所有线程并设置线程池状态为STOPPED，不处理任务队列等内容
 */
void ThreadPool::stop() {
    // 先确保平衡线程已关闭
    // 在设置条件与notify之前获取锁，避免丢失唤醒而引起无限等待
    {
        unique_lock<mutex> state_lock(state_mutex_);
        load_balancer_exit_.store(true);
        pause_cv_.notify_one();  // 假设线程池停止中，需要一次唤醒来让平衡线程继续运行
    }
    {
        unique_lock<mutex> queue_lock(queue_mutex_);
        stopped_.store(true);
        queue_cv_.notify_all();
    }
    if (load_balancer_.joinable()) {
        load_balancer_.join();
    }
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    {
        unique_lock<mutex> state_lock(state_mutex_);
        setState(State::STOPPED);
    }
}

/**
 * @brief 优雅停止线程池(除非你给他传个ShutdownOption::FORCE)
 * @param opt 停止选项: 
 * WAIT_ALL: 等待完成所有任务后停止;
 * WAIT_FOR: 等待一段时间后关闭线程池;
 * FORCE   : 强制关停线程池，丢弃所有任务
 * @param wait_time WAIT_FOR所需的超时时间
 */
void ThreadPool::shutdown(ShutdownOption opt, milliseconds wait_time) {
    {
        unique_lock<mutex> state_lock(state_mutex_);
        if (state_.load() != State::RUNNING && state_.load() != State::PAUSED) {
            return;
        }
        setState(State::SHUTING_DOWN);
    }
    
    switch(opt) {
        case ShutdownOption::WAIT_ALL:
            waitForAllTask();
            break;
        case ShutdownOption::FORCE:
            forceStop();
            return;
        case ShutdownOption::WAIT_FOR:
            if (!waitForRunningTask(wait_time)) {
                forceStop();
                return;
            }
            break;
    }
    stop();
}

/**
 * @brief 暂停线程池
 */
void ThreadPool::pause() {
    unique_lock<mutex> state_lock(state_mutex_);
    if (state_.load() == State::RUNNING) {
        setState(State::PAUSED);
    }
}

/**
 * @brief 恢复线程池运行
 */
void ThreadPool::resume() {
    // 设置状态后工作线程即可恢复运行
    unique_lock<mutex> state_lock(state_mutex_);
    if (state_.load() == State::PAUSED) {
        setState(State::RUNNING);
    }
    
    // notify_all后平衡线程才恢复运行
    pause_cv_.notify_all();
}

/**
 * @return 线程池统计信息
 */
ThreadPool::Status ThreadPool::getStatus() const {
    unique_lock<mutex> lock(status_mutex_);
    Status status = status_;

    // 实时记录活动线程数与负载因子
    status.active_threads = active_threads_.load();
    status.load_factor = calLoadFactor();
    return status;
}

/**
 * @brief 载荷检查函数，根据载荷情况决定创建新线程或是移除闲置线程，可在外部手动触发
 */
void ThreadPool::loadCheck() {
    if (!isRunning()) {
        return;
    }
    unique_lock<mutex> worker_lock(worker_access_mutex_);
    double load_factor = calLoadFactor();
    std::size_t thread_count = thread_count_.load();
    std::size_t pending_task = pending_tasks_.load();

    bool should_expand = false;
    // 扩容条件
    if (thread_count < config_.max_threads) {
        if (load_factor >= config_.expand_factor) {
            should_expand = true;
        } else if (pending_task >= config_.task_limit) {
            // 任务过多而扩容
            should_expand = true;
        } else if (thread_count > 0 && static_cast<double>(pending_task) / thread_count > config_.avg_task_threshold) {
            // 每个线程平均负担任务过多而扩容
            should_expand = true;
        }
        if (should_expand) {
            tryCreateNewThread();
        }
    }
    
    // 移除条件
    if (!should_expand && thread_count > config_.core_threads && load_factor < config_.remove_factor) {
        tryRemoveIdleThread();
    }
}

/**
 * @brief 线程池工作线程所用函数
 * @param thread_index 线程池id，与数组中存储工作线程的下标相对应
 */
void ThreadPool::workerLoop(std::size_t thread_index) {
    while (true) {
        bool have_task = false;
        std::unique_ptr<TaskBase> task;
        {
            unique_lock<mutex> queue_lock(queue_mutex_);
            // 线程应该退出标志位为true时退出线程
            if (thread_should_exit_[thread_index]->load()) {
                return;
            }
            
            if (state_.load() == State::PAUSED) {
                queue_lock.unlock();

                while (state_.load() == State::PAUSED && !stopped_.load()) {
                    std::this_thread::sleep_for(milliseconds(10));           
                }
                if (stopped_.load()) {
                    return;
                }
                queue_lock.lock();
            }
            // 先因检查任务队列状态获取锁
            queue_cv_.wait(queue_lock, [this, thread_index]() { 
                return stopped_.load() || task_queue_.size() > 0 || (thread_should_exit_[thread_index]->load());
            });

            // 如果没有任务，或者强制关停时，线程return
            if (stopped_ || state_.load() == State::FORCE_SHUTING) {
                if (task_queue_.size() == 0 || state_.load() == State::FORCE_SHUTING) {
                    return;
                }
            }
            // 有任务则取出并准备执行
            if (task_queue_.size() > 0) {
                task_queue_.dequeue(task);
                have_task = true;
                active_threads_++;

                if (thread_index != SIZE_MAX) {
                    // 更新线程最后活动时间与线程空闲次数
                    thread_last_active_[thread_index] = chrono::steady_clock::now();
                    thread_idle_count_[thread_index]->store(0);
                }
            }
        }
        
        if (have_task) {
            running_tasks_++;
            try {
                auto start = chrono::steady_clock::now();
                task->execute();
                auto end = chrono::steady_clock::now();
                auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
                double execute_time_ms = static_cast<double>(duration.count()) / 1000.0;
                {
                    unique_lock<mutex> lock(status_mutex_);
                    if (task->taskSuccess()) {
                        double total_time = (status_.avg_tasks_time * status_.tasks_succeeded + execute_time_ms);
                        status_.tasks_succeeded++;
                        status_.avg_tasks_time = total_time / status_.tasks_succeeded;

                    } else {
                        status_.tasks_failed++;
                    }
                }
            } catch (const std::exception& e) {
                unique_lock<mutex> lock(status_mutex_);
                status_.tasks_failed++;
            }

            // 任务结束后重新获取锁，持锁状态下更新线程池数据，保证notify不会丢失
            unique_lock<mutex> queue_lock(queue_mutex_);
            pending_tasks_--;
            running_tasks_--;
            active_threads_--;
            wait_cv_.notify_all();
        }
    }
}

/**
 * @brief 载荷平衡线程所用循环
 */
void ThreadPool::loadBalancerLoop() {
    while (!load_balancer_exit_.load()) {
        {
            unique_lock<mutex> state_lock(state_mutex_);
            pause_cv_.wait(state_lock, [this]() {
                return state_.load() != State::PAUSED || load_balancer_exit_.load();
            });
            if (state_.load() != State::RUNNING) {
                return;
            }
        }
        std::this_thread::sleep_for(config_.balancer_check_interval); // 根据给定间隔检查线程是否空闲

        if (load_balancer_exit_.load()) {
            break;
        }
        loadCheck();
        cleanFinishedThread();
    }
    return;
}

/**
 * @brief 尝试创建新线程。该函数中不应该对worker_access_mutex_再加锁，调用函数loadCheck已经加锁且无其他函数调用该函数
 * @return 新线程是否创建成功
 */
bool ThreadPool::tryCreateNewThread() {
    if (stopped_.load()) {
        return false;
    }
    try {
        std::size_t thread_index = config_.core_threads; // thread_count_.load()并不总是指向下一个可用位点
        // 找出哪个坑可以填补
        for (; thread_index < config_.max_threads && thread_index < workers_.size(); thread_index++) {
            if (thread_should_exit_[thread_index]->load()) {
                // 找到可以填补的坑位
                break;
            }
        }
        if (thread_index >= config_.max_threads) {
            // 一般来说不应该到这里
            return false;
        } else if (thread_index < workers_.size()) {
            // 复用旧线程的坑位，先把旧线程收拾下
            LOG_DEBUG("Reusing index {} for new thread", thread_index);
            if (workers_[thread_index].joinable()) {
                workers_[thread_index].join();
                thread_count_--;
                unique_lock<mutex> status_lock(status_mutex_);
                status_.thread_destroyed++;
            }
            // 用新线程填补坑位
            workers_[thread_index] = std::thread(&ThreadPool::workerLoop, this, thread_index);
            thread_last_active_[thread_index] = chrono::steady_clock::now();
            thread_idle_count_[thread_index]->store(0);
            thread_should_exit_[thread_index]->store(false);
        } else {
            LOG_DEBUG("Creating thread {} by emplace_back", thread_index);
            workers_.emplace_back(&ThreadPool::workerLoop, this, thread_index);
        }

        // 更新数据
        thread_count_++;
        {
            unique_lock<mutex> status_lock(status_mutex_);
            status_.thread_created++;
            std::size_t thread_count = thread_count_.load();
            if (thread_count > status_.peak_thread) {
                status_.peak_thread = thread_count;
            }
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create thread: {}", e.what());
        return false;
    }
}

/**
 * @brief 尝试移除空闲线程。该函数中不应该对worker_access_mutex_再加锁，调用函数loadCheck已经加锁且无其他函数调用该函数
 * @brief 需要留意workers_数组中可能有些线程是之前已关闭的空闲，不要重复关闭
 * @return 是否已移除线程
 */
bool ThreadPool::tryRemoveIdleThread() {
    if (thread_count_ <= config_.core_threads) {
        // 少于最少线程数，不能移除线程了
        return false;
    }

    auto now = chrono::steady_clock::now();

    // 此处暂不加锁，即使外部未持有锁，要移除线程的时候不会创建新线程，不涉及workers_数组的大小变化，读size()暂时认为安全
    std::size_t workers_size = workers_.size();
    for (std::size_t i = config_.core_threads; i < workers_size; i++) {
        if (thread_should_exit_[i]->load()) {
            // 线程已关闭
            continue;
        }

        // 在准备删除的时候检查当前线程活动状态？对于是否在此处理我目前有疑问。
        auto idle_time = chrono::duration_cast<milliseconds>(now - thread_last_active_[i]);
        if (idle_time >= config_.idle_time_threshold) {
            thread_idle_count_[i]->fetch_add(1);
        }

        // 暂时认为检查时空闲大于3次且当前空闲时间大于5秒则移除线程
        bool can_remove = thread_idle_count_[i]->load() >= config_.consecutive_idle_count;
        if (can_remove) {
            LOG_DEBUG("thread {} should be removed for being idle", i);
            {
                unique_lock<mutex> queue_lock(queue_mutex_);
                thread_should_exit_[i]->store(true);
                queue_cv_.notify_all();
            }
            return true;
        }
    }
    return false;
}

/**
 * @brief 清理已结束的线程，更新线程销毁计数
 */
void ThreadPool::cleanFinishedThread() {
    unique_lock<mutex> worker_lock(worker_access_mutex_);
    for (std::size_t i = 0; i < workers_.size(); i++) {
        if (thread_should_exit_[i]->load() && workers_[i].joinable()) {
            workers_[i].join();
            LOG_DEBUG("Thread {} removed", i);
            thread_count_--;
            unique_lock<mutex> status_lock(status_mutex_);
            status_.thread_destroyed++;
        }
    }
}

/**
 * @brief 即时计算负载因子并返回值
 * @return 负载因子，计算公式：当前活动线程数 / 总线程数(不是workers_数组的大小)
 */
double ThreadPool::calLoadFactor() const {
    std::size_t thread_count = thread_count_.load();
    if (thread_count == 0) {
        return 0.0;
    } else {
        std::size_t active_threads = active_threads_.load();
        return static_cast<double>(active_threads) / static_cast<double>(thread_count);
    }
    
}

/**
 * @brief 设置线程池状态并输出日志记录，由外部获取锁后再调用该函数，函数本身不处理锁相关内容
 */
void ThreadPool::setState(State new_state) {
    State old_state = state_.load();
    state_.store(new_state);
    LOG_INFO("Thread pool state switch from {} to {}", stateToString(old_state), stateToString(new_state));
}

/**
 * @brief 强制暂停线程池函数，设置好线程池状态，标记任务被丢弃后再进入stop()回收线程
 */
void ThreadPool::forceStop() {
    {
        unique_lock<mutex> state_lock(state_mutex_);
        if (state_.load() == State::FORCE_SHUTING || state_.load() == State::STOPPED) {
            return;
        }
        setState(State::FORCE_SHUTING);
    }
    {
        // 将余下的任务设为丢弃
        unique_lock<mutex> queue_lock(queue_mutex_);
        while (task_queue_.size() > 0) {
            std::unique_ptr<TaskBase> task;
            task_queue_.dequeue(task);
            task->abandon();
            pending_tasks_--;
            unique_lock<mutex> status_lock(status_mutex_);
            status_.tasks_failed++;
        }
        queue_cv_.notify_all();
        wait_cv_.notify_all();
    }
    stop();
}

/**
 * @brief 从给定路径构建结构体
 * @param file_path std::string类型的文件路径字符串
 */
std::unique_ptr<ThreadPool> makeThreadPool(const std::string& file_path) {
    Config config = loadConfigFromFile(file_path);
    return std::make_unique<ThreadPool>(config);
}

/**
 * @brief 允许用户直接构造配置结构体后，利用该结构体创建线程池，避免每次都要读取config文件
 * @param config 配置参数结构体
 */
std::unique_ptr<ThreadPool> makeThreadPool(const Config& config) {
    validateConfigBasic(config);
    validateConfigExtra(config);

    return std::make_unique<ThreadPool>(config);
}

} // namespace threadpool