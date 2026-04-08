#include "thread_pool_improved.h"
#include "logger.h"

#include <iostream>
#include <thread>

#include <gtest/gtest.h>

using std::size_t;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
namespace chrono = std::chrono;
using threadpool::ThreadPool;
using threadpool::QueueFullPolicy;
using threadpool::Config;
using threadpool::Logger;
using threadpool::LoggerConfig;

/**
 * @brief 生成一个简单的返回任务
 */
std::function<int()> simpleReturnTask(milliseconds time = milliseconds(1000)) {
    return [time]() -> int {
        sleep_for(time);
        return 1;
    };
}

/**
 * @brief 生成一个抛出异常的任务
 */
std::function<int()> exceptionTask(milliseconds time = milliseconds(1000)) {
    return [time]() -> int {
        sleep_for(time);
        throw std::runtime_error("throw a runtime error here");
        return 1;
    };
}

/**
 * @brief 测试提交任务的分类
 */
class SubmitTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig config;
        config.name = "test_thread_pool";
        config.file_path = "./logs.log";
        Logger::getInstance().initialize(config);
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    ThreadPool thread_pool{4, false};
};

/**
 * @brief 测试提交功能能否正常将任务交到队列中，并测试工作线程能否正确提供结果
 */
TEST_F(SubmitTest, SingleThreadSubmit) {
    int int_task = 4;
    std::vector<std::future<int>> int_res;
    int_res.reserve(int_task);
    for (int i = 0; i < int_task; i++) {
        int_res.emplace_back(thread_pool.submitWithResult(simpleReturnTask(milliseconds(10))));
    }
    
    for (auto& r : int_res) {
        ASSERT_NO_THROW(r.get()) << "简单任务不应抛出异常";
    }
    // 等待线程池统计完成任务数，不sleep有可能tasks_succeeded只记录到3就被主线程抢到锁
    sleep_for(milliseconds(1)); 
    ASSERT_EQ(thread_pool.getStatus().tasks_succeeded, int_task) << "所有任务均应已完成, 且应以修改计数器";

    // 测试提交异常任务
    int_res.clear();
    for (int i = 0; i < int_task; i++) {
        int_res.emplace_back(thread_pool.submitWithResult(exceptionTask(milliseconds(10))));
    }
    for (auto& r : int_res) {
        ASSERT_THROW(r.get(), std::runtime_error) << "异常任务理应抛出异常";
    }
    // 等待线程池统计完成任务数，理由类似上方
    sleep_for(milliseconds(1)); 
    ASSERT_EQ(thread_pool.getStatus().tasks_failed, int_task) << "所有异常任务均应被标记为失败";
}

/**
 * @brief 测试多线程提交时线程池能否正确处理
 */
TEST_F(SubmitTest, MultiThreadSubmit) {
    int void_task = 1000;
    int submitter_count = 10;
    std::atomic<std::size_t> counter{0};

    std::vector<std::thread> submitter;
    for (int i = 0; i < submitter_count; i++) {
        submitter.emplace_back([this, &void_task, &counter]() {
            for (int i = 0; i < void_task; i++) {
                thread_pool.submitWithResult([&counter]() { counter++; });
            }
        });
    }
    for (auto& t : submitter) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    // 等待所有任务完成
    sleep_for(milliseconds(500));
    ASSERT_EQ(thread_pool.getStatus().tasks_succeeded, void_task * submitter_count) << "所有任务均应成功";
    ASSERT_EQ(counter.load(), void_task * submitter_count) << "计数器增加值应与任务数量相符";
}

/**
 * 测试动态线程的分类
 */
class DynamicThreadTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig config;
        config.name = "test_thread_pool";
        config.file_path = "./logs.log";
        config.level = threadpool::LogLevel::DEBUG;
        Logger::getInstance().initialize(config);
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    ThreadPool thread_pool{6, true};
};

/**
 * @brief 测试线程池能否正确创建与移除线程，能否复用workers_数组中的空位(已移除线程的位置)
 */
TEST_F(DynamicThreadTest, CreateRemove) {
    for (int i = 0; i < 4; i++) {
        for (int i = 0; i < 4; i++) {
            thread_pool.submitWithResult(simpleReturnTask(milliseconds(500)));
        }
        sleep_for(milliseconds(2500));
    }
    ThreadPool::Status status = thread_pool.getStatus();
    ASSERT_GT(status.peak_thread, 5) << "理应创建过额外线程";
    ASSERT_GT(status.thread_created, 4) << "理应创建过额外线程";
    ASSERT_GT(status.thread_destroyed, 0) << "理应销毁过额外线程";
}

/**
 * @brief 该测试旨在测试当线程运行耗时很长的任务后会不会被判断为空闲线程被移除
 * @brief 当前代码测试结果指出会被移除
 */
TEST_F(DynamicThreadTest, LongTask) {
    for (int i = 0; i < 4; i++) {
        thread_pool.submitWithResult(simpleReturnTask(milliseconds(1000)));
    }
    thread_pool.submitWithResult(simpleReturnTask(milliseconds(3500)));
    sleep_for(milliseconds(5000));
    ASSERT_GT(thread_pool.getStatus().thread_destroyed, 0) << "理应有线程被移除过";
}

/**
 * @brief 测试多个线程短时间高频调用loadCheck，程序能否正确处理多次loadCheck
 */
TEST_F(DynamicThreadTest, LoadCheck) {
    int checker_num = 3;
    std::atomic<bool> finish{false};
    std::vector<std::thread> load_checker;
    for (int i = 0; i < checker_num; i++) {
        load_checker.emplace_back([this, &finish]() {
            while (!finish.load()) {
                thread_pool.loadCheck();
                sleep_for(milliseconds(200));
            }
        });
    }

    for (int i = 0; i < 6; i++) {
        thread_pool.submitWithResult(simpleReturnTask(milliseconds(400)));
    }
    
    sleep_for(milliseconds(2500));
    finish.store(true);
    for (auto& checker : load_checker) {
        if (checker.joinable()) {
            checker.join();
        }
    }
    ThreadPool::Status status = thread_pool.getStatus();
    ASSERT_GT(status.peak_thread, 4) << "多个线程短时间内调用loadCheck后应该创建一个新线程";
    ASSERT_GT(status.thread_destroyed, 0) << "多个线程短时间内调用loadCheck后应该至少移除一个线程";
}

/**
 * @brief 测试不同队满入队策略的分类
 */
class QueueFullPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig config;
        config.name = "test_thread_pool";
        config.file_path = "./logs.log";
        Logger::getInstance().initialize(config);

        for (int i = 0; i < 4; i++) {
            thread_pool.submitWithResult(simpleReturnTask(milliseconds(init_task_time)));
        }
        for (int i = 0; i < 10; i++) {
            thread_pool.submitWithResult(simpleReturnTask(milliseconds(1)));
        }
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    ThreadPool thread_pool{4, 10, QueueFullPolicy::BLOCKING, false};
    int init_task_time{500};
};

/**
 * @brief 测试阻塞入队策略能否正确阻塞进程
 */
TEST_F(QueueFullPolicyTest, Block) {
    auto t1 = chrono::steady_clock::now();
    thread_pool.submitWithResult(simpleReturnTask(milliseconds(1)));
    auto t2 = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<milliseconds>(t2 - t1);
    ASSERT_GE(thread_pool.getStatus().tasks_succeeded, 1) << "至少有一个任务结束后才能停止等待入队";
    ASSERT_GE(elapsed.count(), init_task_time - 10) << "阻塞入队等待时间应接近第一批任务运行时间(10ms抖动)";
}

/**
 * @brief 测试运行期间切换入队策略到DISCARD
 */
TEST_F(QueueFullPolicyTest, Discard) {
    thread_pool.setQueueFullPolicy(QueueFullPolicy::DISCARD);
    for (int i = 0; i < 100; i++) {
        thread_pool.submitWithResult(simpleReturnTask());
    }
    ASSERT_EQ(thread_pool.queueDiscardCount(), 100) << "丢弃入队时，队满期间入队的任务均应被丢弃";
}

/**
 * @brief 测试运行期间切换入队策略到OVERWRITE
 */
TEST_F(QueueFullPolicyTest, Overrun) {
    thread_pool.setQueueFullPolicy(QueueFullPolicy::OVERWRITE);
    for (int i = 0; i < 100; i++) {
        thread_pool.submitWithResult(simpleReturnTask());
    }
    ASSERT_EQ(thread_pool.queueOverrunCount(), 100) << "覆盖入队时, 队满期间入队的任务均应覆盖旧任务";
}

/**
 * 测试线程池状态的分类
 */
class StateTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig config;
        config.name = "test_thread_pool";
        config.file_path = "./logs.log";
        Logger::getInstance().initialize(config);
        for (std::size_t i = 0; i < thread_pool.coreThreadCount() + extra_tasks; i++) {
            thread_pool.submitWithResult(simpleReturnTask());
        }
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    ThreadPool thread_pool{4, false};
    int extra_tasks{10};
};

/**
 * @brief 测试暂停，恢复线程池，以及等待所有任务结束后关闭线程的过程中能否正确切换线程池状态
 */
TEST_F(StateTest, BasicState) {
    thread_pool.pause();
    ASSERT_TRUE(thread_pool.isPaused()) << "停止后线程池理应处于PAUSED状态";
    ASSERT_THROW(thread_pool.submitWithResult(simpleReturnTask()), std::runtime_error) << "线程池停止期间提交任务应抛出异常";
    sleep_for(milliseconds(3000));  // 睡眠3秒可以顺带验证pause期间会不会关停线程

    thread_pool.resume();
    ASSERT_TRUE(thread_pool.isRunning()) << "恢复后线程池理应处于RUNNING状态";
    ASSERT_NO_THROW(thread_pool.submitWithResult(simpleReturnTask())) << "恢复后的线程提交任务时理应正常能提交";
    
    thread_pool.shutdown(ThreadPool::ShutdownOption::WAIT_ALL);
    ASSERT_EQ(thread_pool.getStatus().tasks_succeeded, thread_pool.coreThreadCount() + extra_tasks + 1) << "所有任务均应已完成";
}

/**
 * @brief 测试条件等待关闭线程池过程中能否正确关闭线程，能否正确丢弃任务
 */
TEST_F(StateTest, ShutdownWaitFor) {
    sleep_for(milliseconds(10));
    thread_pool.shutdown(ThreadPool::ShutdownOption::WAIT_FOR, milliseconds(1100));
    ThreadPool::Status status = thread_pool.getStatus();
    ASSERT_GT(status.tasks_succeeded, thread_pool.coreThreadCount()) << "应该有足够时间让一个线程跑完任务接取下一个任务";
    ASSERT_LT(status.tasks_succeeded, thread_pool.coreThreadCount() + extra_tasks) << "给定时间不可能完成所有任务";
    ASSERT_GT(status.tasks_failed, 0) << "强制关闭时应该将任务设为丢弃并视为任务失败";
}

/**
 * @brief 测试强制关闭线程池函数，并检查丢弃的任务是否正确处理了，不会无限等待
 */
TEST_F(StateTest, ShutdownForce) {
    int catch_count = 10;   // 提供10个任务用于捕捉丢弃任务时的异常
    std::vector<std::future<int>> res;
    for (int i = 0; i < catch_count; i++) {
        res.emplace_back(thread_pool.submitWithResult(simpleReturnTask()));
    }
    sleep_for(milliseconds(10));
    thread_pool.shutdown(ThreadPool::ShutdownOption::FORCE);
    ThreadPool::Status status = thread_pool.getStatus();
    for (auto& r : res) {
        ASSERT_THROW(r.get(), std::runtime_error) << "被丢弃的任务应该抛出异常";
    }
    ASSERT_EQ(status.tasks_succeeded, thread_pool.coreThreadCount()) << "提交任务后立即关停应该只能完成最先入队的4个任务";
    ASSERT_EQ(status.tasks_failed, extra_tasks + catch_count) << "失败任务计数应为额外任务与捕捉任务数之和";
}

/**
 * 测试等待函数的分类
 */
class WaitTest : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig config;
        config.name = "test_thread_pool";
        config.file_path = "./logs.log";
        Logger::getInstance().initialize(config);

        for (std::size_t i = 0; i < thread_pool.coreThreadCount() * epoch; i++) {
            thread_pool.submitWithResult(simpleReturnTask(milliseconds(sleep_time)));
        }
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    ThreadPool thread_pool{4, false};
    int sleep_time{500};
    int epoch{2};
};

/**
 * @brief 测试等待函数能否阻塞直到所有任务完成
 */
TEST_F(WaitTest, WaitAll) {
    auto t1 = chrono::steady_clock::now();
    thread_pool.waitForAllTask();
    auto t2 = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<milliseconds>(t2 - t1).count();
    ASSERT_GE(elapsed, sleep_time * epoch) << "Wait all操作应等待直至所有任务完成";
}

/**
 * @brief 测试条件等待能否正常返回结果，能否避免线程无限阻塞
 */
TEST_F(WaitTest, WaitFor) {
    auto t1 = chrono::steady_clock::now();
    bool should_fail = thread_pool.waitForRunningTask(milliseconds(sleep_time));
    auto t2 = chrono::steady_clock::now();
    bool should_success = thread_pool.waitForRunningTask(milliseconds(sleep_time + (sleep_time >> 1)));
    auto t3 = chrono::steady_clock::now();
    
    ThreadPool::Status status = thread_pool.getStatus();
    auto failed_elapsed = chrono::duration_cast<milliseconds>(t2 - t1).count();
    auto success_elapsed = chrono::duration_cast<milliseconds>(t3 - t2).count();
    ASSERT_FALSE(should_fail) << "第1次等待仅等待了1轮任务的耗时, 理应失败";
    ASSERT_TRUE(should_success) << "第2次等待再等了1.5轮任务的耗时, 共等了2.5轮任务耗时, 大于提供的2轮任务, 应成功";
    ASSERT_GE(failed_elapsed, sleep_time) << "等待失败耗时应大于给定等待时间";
    ASSERT_LT(success_elapsed, sleep_time * 1.5) << "等待成功耗时应少于给定等待时间";
    ASSERT_EQ(status.tasks_succeeded, epoch * thread_pool.coreThreadCount()) << "2次等待过后线程池任务应均已完成";
}

/**
 * @brief 测试当某个线程等待时，其他线程添加任务的情况下，线程操作是否符合预期。
 * @brief 目前认为等待线程会持续等待至新任务也完成后才被唤醒。
 */
TEST_F(WaitTest, MultiThreadWaitAll) {
    int add_epoch = epoch <= 1? 2 : epoch + (epoch >> 1);
    auto func = [this, &add_epoch]() {
        sleep_for(milliseconds(sleep_time / 10 * 9 * epoch));
        for (std::size_t i = 0; i < thread_pool.coreThreadCount() * add_epoch; i++) {
            ASSERT_NO_THROW(
                thread_pool.submitWithResult(simpleReturnTask(milliseconds(sleep_time)))
            ) << "线程在wait all期间不应影响其他线程提交任务";
        }
    };
    std::thread submitter(func);

    auto t1 = chrono::steady_clock::now();
    thread_pool.waitForAllTask();
    auto t2 = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<milliseconds>(t2 - t1).count();
    if (submitter.joinable()) {
        submitter.join();
    }
    ThreadPool::Status status = thread_pool.getStatus();

    // 加了1.5轮任务，总耗时为2.5轮，只需要验证wait的时候有新的任务来了就行，所以仅检查大于等于2倍原本等待时间
    ASSERT_GT(elapsed, sleep_time * (epoch * 2)) << "等待所有任务时间应大于原先提供的任务耗时";
    ASSERT_EQ(status.tasks_succeeded, thread_pool.coreThreadCount() * (add_epoch + epoch)) << "所有任务均应已完成";
}

/**
 * 基准测试分类
 */
class PerformanceBenchmark : public ::testing::Test {
protected:
    void SetUp() override {
        LoggerConfig logger_config;
        logger_config.name = "test_thread_pool";
        logger_config.file_path = "./logs.log";
        Logger::getInstance().initialize(logger_config);
        config.enable_dynamic_thread = false;
    }
    void TearDown() override {
        Logger::getInstance().shutdown();
    }
    Config config;
};

/**
 * @brief 吞吐量基准测试
 */
TEST_F(PerformanceBenchmark, ThroughputBenchmark) {
    config.core_threads = config.max_threads = 8;
    constexpr int TASK_NUM = 100000;
    constexpr int TASK_DURATION_US = 100;

    std::unique_ptr<ThreadPool> thread_pool = threadpool::makeThreadPool(config);

    std::atomic<int> task_completed{0};

    auto start_time = chrono::steady_clock::now();

    std::vector<std::future<void>> futures;
    futures.reserve(TASK_NUM);
    
    for (int i = 0; i < TASK_NUM; i++) {
        futures.emplace_back(thread_pool->submitWithResult([&task_completed]() {
            auto begin = chrono::steady_clock::now();
            while (true) {
                auto duration = chrono::steady_clock::now() - begin;
                if (chrono::duration_cast<chrono::microseconds>(duration).count() >= TASK_DURATION_US) {
                    break;
                }
            }
            task_completed++;
        }));
    }

    for (auto& res : futures) {
        res.get();
    }
    auto end_time = chrono::steady_clock::now();
    auto duration = chrono::duration_cast<milliseconds>(end_time - start_time);

    double task_per_sec = static_cast<double>(TASK_NUM) * 1000 / duration.count();
    double avg_task_time_ms = static_cast<double>(duration.count()) / TASK_NUM;

    // 加点分隔符方便定位
    
    LOG_INFO(
        "\n--------------------\n"
        "吞吐量测试结果:\n"
        "线程数: {}\n"
        "总任务数: {}\n"
        "总耗时: {} 毫秒\n"
        "吞吐量: {}个任务/秒\n"
        "平均任务耗时: {}毫秒/任务\n"
        "--------------------",
        config.core_threads,
        TASK_NUM,
        duration.count(),
        task_per_sec,avg_task_time_ms
    );


    ASSERT_GE(task_per_sec, 10000) << "1秒10000次加法都做不到，效率过低";
    ASSERT_EQ(task_completed.load(), TASK_NUM) << "任务完成计数应与任务数一致";
    ASSERT_EQ(thread_pool->getStatus().tasks_succeeded, TASK_NUM) << "所有任务均应标记已完成";
}

/**
 * @brief 任务延迟基准测试
 */
TEST_F(PerformanceBenchmark, LatencyBenchmark) {
    config.core_threads = config.max_threads = 4;
    ThreadPool thread_pool(config);
    constexpr int SAMPLE_NUM = 10000;
    std::vector<chrono::nanoseconds> latencies;
    latencies.reserve(SAMPLE_NUM);

    for (int i = 0; i < SAMPLE_NUM; i++) {
        auto submit_time = chrono::steady_clock::now();

        auto future = thread_pool.submitWithResult([submit_time]() -> chrono::nanoseconds {
            auto execution_start = chrono::steady_clock::now();
            return chrono::duration_cast<chrono::nanoseconds>(execution_start - submit_time);
        });

        chrono::nanoseconds latency = future.get();
        latencies.emplace_back(latency);

        if (i % 100 == 0) {
            sleep_for(milliseconds(1));
        }
    }

    std::sort(latencies.begin(), latencies.end());
    auto avg_latency = std::accumulate(latencies.begin(), latencies.end(), chrono::nanoseconds(0)) / SAMPLE_NUM;
    auto median_latency = latencies[SAMPLE_NUM / 2];
    auto p95_latency = latencies[static_cast<int>(SAMPLE_NUM * 0.95)];
    auto p99_latency = latencies[static_cast<int>(SAMPLE_NUM * 0.99)];
    auto max_latency = latencies.back();

    // 加点分隔符方便定位
    LOG_INFO(
        "\n--------------------\n"
        "延迟基准测试结果(单位微秒):\n"
        "平均延迟: {}\n"
        "中位延迟: {}\n"
        "95%延迟: {}\n"
        "99%延迟: {}\n"
        "最大延迟: {}\n"
        "--------------------",
        avg_latency.count() / 1000,
        median_latency.count() / 1000,
        p95_latency.count() / 1000,
        p99_latency.count() / 1000,
        max_latency.count() / 1000
    );

    ASSERT_LT(avg_latency.count(), 10000000);
    ASSERT_LT(p95_latency.count(), 50000000);
}

/**
 * @brief 长期运行稳定性测试
 */
TEST_F(PerformanceBenchmark, LongRunningStability) {
    config.core_threads = config.max_threads = 4;
    config.max_queue_size = 200;

    constexpr int TEST_DURATION_SEC = 30;
    constexpr int TASK_RATE_PER_SEC = 1000;

    std::atomic<bool> running{true};
    std::atomic<int> submitted{0};
    std::atomic<int> completed{0};
    std::atomic<int> failed{0};

    ThreadPool thread_pool(config);

    std::thread submitter([&]() {
        while (running.load()) {
            // 提交任务后休眠0.1秒，再继续提交，因此单轮循环提交每秒任务量的十分之一
            for (int i = 0; i < TASK_RATE_PER_SEC / 10; i++) {
                if (!running.load()) {
                    break;
                }

                try {
                    thread_pool.submitWithResult([&completed]() {
                        sleep_for(milliseconds(500));
                        completed++;
                    });
                    submitted++;
                } catch (...) {
                    failed++;
                }
            }
            sleep_for(milliseconds(100));
        }
    });

    std::thread monitor([&]() {
        int last_completed = 0;
        while (running.load()) {
            sleep_for(chrono::seconds(5));

            int cur_completed = completed.load();
            int interval_completed = cur_completed - last_completed;
            last_completed = cur_completed;

            ThreadPool::Status status = thread_pool.getStatus();
            LOG_INFO(
                "从上次检查到目前为止, 线程池效率: {}个任务/秒, 活跃线程数: {}, 任务队列大小: {}",
                interval_completed / 5,
                status.active_threads,
                thread_pool.taskCount()
            );
        }
    });

    sleep_for(chrono::seconds(TEST_DURATION_SEC));
    running.store(false);

    if (submitter.joinable()) {
        submitter.join();
    }
    if (monitor.joinable()) {
        monitor.join();
    }

    // 调用waitForRunningTask相对而言比调用waitForAll安全
    while (!thread_pool.waitForRunningTask(chrono::seconds(1))) {
        continue;
    }

    ThreadPool::Status last_status = thread_pool.getStatus();
    double success_rate = static_cast<double>(last_status.tasks_succeeded) / submitted.load();

    // 加点分隔符方便定位
    LOG_INFO(
        "\n--------------------\n"
        "长期运行稳定性测试结果: \n"
        "运行时间: {}秒\n"
        "提交任务数: {}\n"
        "完成任务数: {}\n"
        "成功率: {}\n"
        "平均任务时间: {}毫秒\n"
        "--------------------",
        TEST_DURATION_SEC,
        submitted.load(),
        last_status.tasks_succeeded,
        success_rate,
        last_status.avg_tasks_time
    );

    ASSERT_GT(success_rate, 0.99) << "长期运行稳定性测试预期希望任务成功率达99%";
    ASSERT_EQ(last_status.tasks_failed, 0) << "长期运行稳定性测试预期希望任务失败数为0";
}

/**
 * @brief 压力测试，测试10秒内4个线程提交大量带返回值的简单任务时，8线程线程池的吞吐量
 */
TEST_F(PerformanceBenchmark, FutureTaskThroughput) {
    constexpr int RUN_TIME_SEC = 10;    // 压力测试时长
    constexpr int SUBMITTER_COUNT = 4;  // 提交任务线程个数
    
    auto thread_pool = threadpool::makeThreadPool("./../../config/throughput_benchmark.json");

    std::atomic<bool> running{true};
    std::atomic<std::size_t> submitted{0};
    std::vector<std::thread> submitters;

    auto begin_time = chrono::steady_clock::now();
    for (int i = 0; i < SUBMITTER_COUNT; i++) {
        submitters.emplace_back([&]() {
            while (running.load()) {
                thread_pool->submitWithResult([]() {
                    thread_local int a = 0;
                    a++;
                });
                submitted++;
            }
        });
    }
    
    sleep_for(chrono::seconds(RUN_TIME_SEC));
    running.store(false);
    thread_pool->waitForAllTask();

    auto end_time = chrono::steady_clock::now();

    for (auto& submitter : submitters) {
        if (submitter.joinable()) {
            submitter.join();
        }
    }
    auto duration = chrono::duration_cast<milliseconds>(end_time - begin_time).count();
    ThreadPool::Status status = thread_pool->getStatus();
    LOG_INFO("带返回值任务耗时: {}毫秒, 吞吐量: {}\n{}", duration, status.tasks_succeeded / RUN_TIME_SEC, status);

    if (thread_pool->getQueueFullPolicy() == QueueFullPolicy::BLOCKING) {
        ASSERT_EQ(submitted.load(), status.tasks_succeeded) << "阻塞入队时所有任务均应被完成";
    } else {
        size_t task_count = submitted.load();
        size_t abandon_count = task_count - status.tasks_succeeded;
        LOG_INFO("丢弃任务数为: {}, 丢弃率: {}%", abandon_count, static_cast<double>(abandon_count) / task_count);
    }
    
    EXPECT_GT(status.tasks_succeeded / RUN_TIME_SEC, 200000) << "吞吐量低于20万每秒，效率有点低下了";
}

/**
 * @brief 压力测试，测试10秒内4个线程提交大量简单普通任务时，8线程线程池的吞吐量
 */
TEST_F(PerformanceBenchmark, NormalTaskThroughput) {
    constexpr int RUN_TIME_SEC = 10;    // 压力测试时长
    constexpr int SUBMITTER_COUNT = 4;  // 提交任务线程个数
    
    auto thread_pool = threadpool::makeThreadPool("./../../config/throughput_benchmark.json");

    std::atomic<bool> running{true};
    std::atomic<long long> counter{0};
    std::vector<std::thread> submitters;

    auto begin_time = chrono::steady_clock::now();
    for (int i = 0; i < SUBMITTER_COUNT; i++) {
        submitters.emplace_back([&]() {
            while (running.load()) {
                auto task = std::make_unique<threadpool::IncrementTask>(counter);
                thread_pool->submit(std::move(task));
            }
        });
    }
    
    sleep_for(chrono::seconds(RUN_TIME_SEC));
    running.store(false);

    thread_pool->waitForAllTask();

    auto end_time = chrono::steady_clock::now();

    for (auto& submitter : submitters) {
        if (submitter.joinable()) {
            submitter.join();
        }
    }
    auto duration = chrono::duration_cast<milliseconds>(end_time - begin_time).count();
    ThreadPool::Status status = thread_pool->getStatus();
    double throughput = static_cast<double>(status.tasks_succeeded) / duration * 1000;
    
    LOG_INFO("普通任务耗时: {}毫秒, 吞吐量: {}\n{}", duration, throughput, status);

    ASSERT_EQ(counter.load(), status.tasks_succeeded) << "外部计数器与任务完成数应该是同步计数的";    
    EXPECT_GT(throughput, 200000) << "吞吐量低于20万每秒，效率有点低下了";
}

/**
 * @brief 少量小型任务耗时基准测试
 */
TEST_F(PerformanceBenchmark, ShortJobDurationBenchmark) {
    const int NUM_THREADS = 4;
    const int NUM_TASKS = 1000;
    ThreadPool pool(NUM_THREADS);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<int>> futures;
    for (int i = 0; i < NUM_TASKS; ++i) {
        futures.push_back(
            pool.submitWithResult([i]() -> int {
                // 模拟一些计算工作
                int result = 0;
                for (int j = 0; j < 1000; ++j) {
                    result += i * j;
                }
                return result;
            })
        );
    }
    
    // 等待所有任务完成
    int total = 0;
    for (auto& future : futures) {
        total += future.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    LOG_INFO("执行{}个任务耗时: {}ms", NUM_TASKS, duration.count());
    // 验证所有任务都完成了
    EXPECT_EQ(futures.size(), NUM_TASKS);
    EXPECT_GT(total, 0);
    
}

int main(int argc, char **argv) {
    // Windows环境下防止输出中文乱码
    #ifdef _WIN32
        system("chcp 65001");
    #endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}