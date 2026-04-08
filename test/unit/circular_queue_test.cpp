#include "circular_queue.h"
#include "mpmc_blocking_queue.h"

#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>
#include <array>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using threadpool::CircularQueue;
using threadpool::MPMCBlockingQueue;

using std::cout;
using std::endl;
using std::vector;
using std::thread;
using std::this_thread::sleep_for;
namespace chrono = std::chrono;
using HRClock = chrono::high_resolution_clock;

/**
 * @brief 测试循环队列的分类
 */
class CircularQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        q.pushBack(1);
        q.pushBack(2);
        q.pushBack(3);
    }
    CircularQueue<int> q{5};
    CircularQueue<int> empty_q;
};

/**
 * @brief 测试循环队列基础功能是否正确运行，如入队、出队、队空、队满等操作
 */
TEST_F(CircularQueueTest, BasicFunctionality) {
    q.pushBack(10);
    int a = 20;
    q.pushBack(a);
    int b = 30;
    q.pushBack(std::move(b));
    q.emplaceBack(40);
    ASSERT_EQ(q.size(), 6 > q.capacity() ? q.capacity() : 6) << "q.size() should be 6 or q.capacity() now";
    ASSERT_FALSE(q.empty()) << "q shoule not be empty now";

    while (!q.empty()) {
        q.popFront();
    }
    ASSERT_TRUE(q.empty());
    ASSERT_FALSE(q.full());

    // 再次塞满队列后尝试pop所有元素
    for (std::size_t i = 1; i <= q.capacity() + 1; i++) {
        q.pushBack(i);
    }
    ASSERT_TRUE(q.full());
    ASSERT_FALSE(q.empty());

    while (!q.empty()) {
        q.popFront();
    }

    ASSERT_THROW(q.popFront(), std::runtime_error) << "Should throw when attemping pop while queue is empty";

    q.clear();
    ASSERT_TRUE(q.empty());
    ASSERT_THROW(q.popFront(), std::runtime_error) << "Should throw when attemping pop while queue is just cleared";
}

/**
 * @brief 测试大量数据入队时能否正确处理溢出，reset后是否正确重置计数器
 */
TEST_F(CircularQueueTest, Overrun) {
    q.pushBack(4);
    q.pushBack(5);
    ASSERT_TRUE(q.full());
    for (int i = 1; i <= 20; i++) {
        q.pushBack(1);
        ASSERT_TRUE(q.full());
        ASSERT_EQ(q.overrunCount(), i);
    }
    q.clear();
    q.resetCounter();
    ASSERT_EQ(q.overrunCount(), 0);
    ASSERT_TRUE(q.empty());

    for (std::size_t i = 1; i <= 20; i++) {
        q.pushBack(1);
        if (i > q.capacity()) {
            ASSERT_EQ(q.overrunCount(), i - 5);
        }
    }
}

/**
 * @brief 测试队列的随机访存操作是否正确实现，at函数访问非法下标也没有抛出异常，以及能否通过[]或者at函数修改队列内容
 * @brief []运算符重载因有assert宏在函数内，此处未测试访问非法下标会不会正常输出某个值(UB)
 */
TEST_F(CircularQueueTest, AccessTest) {
    srand(time(nullptr));
    ASSERT_EQ(q.front(), 1);
    for (int i = 0; i < 20; i++) {
        std::size_t random_idx = rand() % (2 * (q.capacity() + 1));
        if (random_idx >= q.size()) {
            ASSERT_THROW(q.at(random_idx), std::out_of_range) << "Should throw when using at(>=q.size())";
        } else {
            ASSERT_EQ(q.at(random_idx), random_idx + 1);    // 队列存放的是1, 2, 3下标对应0, 1, 2
        }
    }

    q.at(0) = 10;
    q[1] = 6;
    ASSERT_EQ(q[0], 10);
    ASSERT_EQ(q.at(1), 6);
}

/**
 * @brief 测试一个容量为0的队列能否正确处理入队、出队、队空、队满等操作
 */
TEST_F(CircularQueueTest, EmptyQueue) {
    ASSERT_TRUE(empty_q.empty());
    ASSERT_FALSE(empty_q.full());
    ASSERT_EQ(empty_q.size(), 0);
    ASSERT_EQ(empty_q.capacity(), 0);

    int num = 2;
    empty_q.pushBack(1);
    empty_q.pushBack(num);
    empty_q.emplaceBack(3);
    empty_q.emplaceBack(std::move(num));

    ASSERT_TRUE(empty_q.empty()) << "After push, empty_q should still be empty";
    ASSERT_FALSE(empty_q.full());
    ASSERT_EQ(empty_q.size(), 0);
    ASSERT_EQ(empty_q.overrunCount(), 0) << "Push does nothing when capacity == 0, overrun count won't work";
    ASSERT_THROW(empty_q.popFront(), std::runtime_error) << "Pop empty queue should throw";
}

/**
 * @brief 测试MPMC阻塞队列的分类
 */
class MPMCBlockingQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 10; i++) {
            int num = i;
            mpmc_queue.enqueue(std::move(num));
        }
    }
    MPMCBlockingQueue<int> mpmc_queue{10};
};

/**
 * @brief 测试MPMC阻塞队列的基本入队与出队操作能否在单生产者单消费者的情况下正确实现
 */
TEST_F(MPMCBlockingQueueTest, BasicEnqueueDequeue) {
    // 初始状态队列满，必定是先出队后入队
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    // 生产者
    thread pusher([this, &queue_mutex, &queue_cv]() {
        for (int i = 0; i < 10; i++) {
            std::size_t size = 0;
            {
                std::unique_lock<std::mutex> queue_lock(queue_mutex);
                if (mpmc_queue.size() == mpmc_queue.capacity()) {
                    queue_cv.wait(queue_lock, [this]() { return mpmc_queue.size() != mpmc_queue.capacity(); });
                }
                mpmc_queue.enqueue(std::move(1));
                size = mpmc_queue.size();
                queue_cv.notify_all();
            }
            ASSERT_EQ(size, 10);

            // 加sleep防止短时间多次生产导致消费者预测结果有误
            sleep_for(chrono::milliseconds(1));
        }
    });
    // 消费者
    thread poper([this, &queue_mutex, &queue_cv]() {
        int pop_res = 0;
        for (int i = 0; i < 10; i++) {
            std::size_t size = 0;
            {
                std::unique_lock<std::mutex> queue_lock(queue_mutex);
                if (mpmc_queue.size() == 0) {
                    queue_cv.wait(queue_lock, [this]() { return mpmc_queue.size() > 0; });
                }
                mpmc_queue.dequeue(pop_res);
                size = mpmc_queue.size();
                queue_cv.notify_all();
            }
            ASSERT_EQ(size, 9);

            // 加sleep防止短时间多次消费导致生产者预测结果有误
            sleep_for(chrono::milliseconds(1));
        }
    });

    if (pusher.joinable()) {
        pusher.join();
    }
    if (poper.joinable()) {
        poper.join();
    }
}

/**
 * @brief 测试MPMC阻塞队列的立即入队与条件入队函数能否正确强制入队
 */
TEST_F(MPMCBlockingQueueTest, NoWaitAndIfNotFull) {
    ASSERT_EQ(mpmc_queue.size(), mpmc_queue.capacity()) << "mpmc_queue should be full now";
    for (int i = 1; i <= 10; i++) {
        mpmc_queue.enqueueNoWait(100);
        ASSERT_EQ(mpmc_queue.overrunCount(), i);
    }
    ASSERT_EQ(mpmc_queue.overrunCount(), 10) << "There should be 10 data covered";

    for (int i = 1; i <= 10; i++) {
        bool res = mpmc_queue.enqueueIfNotFull(std::move(10));
        ASSERT_FALSE(res);
    }
    ASSERT_EQ(mpmc_queue.discardCount(), 10) << "There should be 10 data discarded";
}

/**
 * @brief 测试有限等待入队函数
 */
TEST_F(MPMCBlockingQueueTest, DequeueWaitForTest) {
    for (int i = 0; i < 20; i++) {
        int num = 0;
        auto t1 = HRClock::now();
        bool res = mpmc_queue.dequeueWaitFor(num, chrono::milliseconds(5));
        auto t2 = HRClock::now();
        auto time = chrono::duration_cast<chrono::milliseconds>(t2 - t1);
        if (i < 10) {
            ASSERT_TRUE(res);
        } else {
            ASSERT_FALSE(res);
        }
    }
}

/**
 * @brief 测试无限等待入队出队函数是否正确等待
 */
TEST_F(MPMCBlockingQueueTest, BlockingEnqueueDequeueTest) {
    // 测试阻塞入队部分
    while (mpmc_queue.size() < mpmc_queue.capacity()) {
        mpmc_queue.enqueue(10);
    }
    auto thread1 = thread([this]() {
        sleep_for(chrono::milliseconds(500));
        int ignore = 0;
        mpmc_queue.dequeue(ignore);
    });
    auto t1 = HRClock::now();
    mpmc_queue.enqueue(10);
    auto t2 = HRClock::now();

    // 清空队列后测试阻塞出队
    while (mpmc_queue.size() > 0 ) {
        int ignore = 0;
        mpmc_queue.dequeue(ignore);
    }

    auto thread2 = thread([this]() {
        sleep_for(chrono::milliseconds(500));
        mpmc_queue.enqueue(1000);
    });
    int res = 0;
    auto t3 = HRClock::now();
    mpmc_queue.dequeue(res);
    auto t4 = HRClock::now();

    // 出入队列耗时统计
    auto time1 = chrono::duration_cast<chrono::milliseconds>(t2 - t1);
    ASSERT_GE(time1.count(), 500) << "time consumed for last enqueue should be longer than 500 milliseconds";
    
    auto time2 = chrono::duration_cast<chrono::milliseconds>(t4 - t3);
    ASSERT_GE(time2.count(), 500) << "time consumed for last dequeue should be longer than 500 milliseconds";

    if (thread1.joinable()) {
        thread1.join();
    }
    if (thread2.joinable()) {
        thread2.join();
    }
}

/**
 * @brief 测试队列分别在多消费者和多生产者环境下，能否正确处理入队出队操作
 */
TEST_F(MPMCBlockingQueueTest, MultiProducerConsumer) {
    vector<thread> consumers;
    std::array<int, 10> count{};
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        consumers.emplace_back([this, &sum, &count]() {
            int num = 0;
            mpmc_queue.dequeue(num);
            sum += num;
            count[num]++;
        });
    }
    vector<thread> producers;
    for (int i = 0; i < 10; i++) {
        producers.emplace_back([this](int num) {
            mpmc_queue.enqueue(std::move(num));
        }, i);
    }

    for (auto& consumer : consumers) {
        if (consumer.joinable()) {
            consumer.join();
        }
    }
    
    for (auto& producer : producers) {
        if (producer.joinable()) {
            producer.join();
        }
    }
    ASSERT_TRUE(mpmc_queue.size() == 0);
    ASSERT_EQ(sum, 90) << "From 0 - 9, sum * 2 should be 90";
    for (const int& c : count) {
        ASSERT_EQ(c, 2) << "Num count should all be 2 for num being enqueued twice";
    }
}

int main(int argc, char **argv) {
    // Windows环境下防止输出中文乱码
    #ifdef _WIN32
        system("chcp 65001");
    #endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}