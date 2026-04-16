#include "thread_pool.h"
#include "logger.h"

#include <fstream>

#include <gtest/gtest.h>

using std::string;
using std::ofstream;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using threadpool::QueueFullPolicy;
using threadpool::ThreadPool;
using threadpool::Config;
using threadpool::LoggerConfig;
namespace fs = std::filesystem;

const fs::path CONFIG_DIR = fs::current_path() / "config_test";

/**
 * @brief 往可运行文件所在目录构建临时配置文件
 */
void createJSONFile() {
    std::string config_str = R"({
        "core_threads": 4,
        "max_threads": 8,
        "max_queue_size": 500,
        "queue_full_policy": "BLOCKING",
        "idle_time_threshold": 500
    })";
    ofstream config_json(CONFIG_DIR / "JSON_to_config.json");
    if (!config_json.is_open()) {
        throw std::runtime_error("Failed to open " + (CONFIG_DIR / "JSON_to_config.json").string());
    }
    config_json << config_str;
    config_json.close();

    std::string logger_config_str = R"({
        "name": "test_name",
        "file_path": "./logs/thread_pool.log",
        "level": "DEBUG",
        "enable_console": true,
        "enable_file": false,
        "max_file_size": 10485760,
        "max_files": 20,
        "auto_flush": false
    })";
    ofstream logger_config_json(CONFIG_DIR / "JSON_to_logger_config.json");
    if (!logger_config_json.is_open()) {
        throw std::runtime_error("Failed to open " + (CONFIG_DIR / "JSON_to_logger_config.json").string());
    }
    logger_config_json << logger_config_str;
    logger_config_json.close();
}

/**
 * @brief 测试配置验证函数的分类
 */
class ValidateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    Config config;
};

/**
 * @brief 测试正确的配置能否通过验证函数
 */
TEST_F(ValidateTest, ValidConfig) {
    config = threadpool::loadConfigFromString(R"({
        "core_threads":2,
        "max_threads":6,
        "max_queue_size":256,

        "balancer_check_interval_ms":200,
        "expand_factor":0.7,
        "remove_factor":0.3,
        "avg_task_threshold":3.0,
        "idle_time_threshold_ms":1500,
        "consecutive_idle_count":5
    })");
    ASSERT_NO_THROW(threadpool::validateConfigBasic(config));
    threadpool::validateConfigExtra(config);

    // 检查JSON中显式设置的值
    ASSERT_EQ(config.core_threads, 2);
    ASSERT_EQ(config.max_threads, 6);
    ASSERT_EQ(config.max_queue_size, 256);
    ASSERT_EQ(config.balancer_check_interval.count(), 200);
    ASSERT_EQ(config.expand_factor, 0.7);
    ASSERT_EQ(config.remove_factor, 0.3);
    ASSERT_EQ(config.avg_task_threshold, 3.0);
    ASSERT_EQ(config.idle_time_threshold.count(), 1500);
    ASSERT_EQ(config.consecutive_idle_count, 5);

    // 检查未设置字段是否保持默认值
    ASSERT_EQ(config.policy, QueueFullPolicy::BLOCKING);
    ASSERT_EQ(config.task_limit, 24);
    ASSERT_EQ(config.enable_dynamic_thread, true);
}

/**
 * @brief 测试非法参数能否通过验证函数
 */
TEST_F(ValidateTest, InvalidConfig) {
    config.core_threads = 0;
    ASSERT_THROW(threadpool::validateConfigBasic(config), std::invalid_argument);

    config.core_threads = config.max_threads + 1;
    ASSERT_THROW(threadpool::validateConfigBasic(config), std::invalid_argument);

    config.core_threads = Config().core_threads;
    config.max_queue_size = 0;
    ASSERT_THROW(threadpool::validateConfigBasic(config), std::invalid_argument);

    config.max_queue_size = Config().max_queue_size;
    config.expand_factor = config.remove_factor;
    ASSERT_THROW(threadpool::validateConfigBasic(config), std::invalid_argument);

    config.expand_factor = 2.0;
    ASSERT_THROW(threadpool::validateConfigBasic(config), std::invalid_argument);
}

/**
 * @brief 测试额外配置检验能否正常发出警告
 */
TEST_F(ValidateTest, WarningConfig) {
    config.max_threads = std::thread::hardware_concurrency() * 10;
    config.max_queue_size = 2000000;
    config.enable_dynamic_thread = true;
    config.task_limit = 2;
    config.balancer_check_interval = milliseconds(5);
    config.idle_time_threshold = milliseconds(5);
    ASSERT_NO_THROW(threadpool::validateConfigBasic(config));
    threadpool::validateConfigExtra(config);
}

/**
 * @brief 验证JSON文件与自定义结构体之间转换的分类
 */
class JSONConvertion : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    const fs::path LOGGERCONFIG_TO_JSON = CONFIG_DIR / "logger_config_to_JSON.json";
    const fs::path JSON_TO_LOGGERCONFIG = CONFIG_DIR / "JSON_to_logger_config.json";
};

/**
 * @brief 测试将Config结构体写入JSON文件，同时会测试policy能否写入JSON文件
 */
TEST_F(JSONConvertion, Config2JSON) {
    const fs::path CONFIG_TO_JSON = CONFIG_DIR / "config_to_JSON.json";

    Config config_val;

    config_val.core_threads = 2;
    config_val.max_threads = 6;
    config_val.max_queue_size = 256;
    config_val.policy = QueueFullPolicy::OVERWRITE;

    config_val.balancer_check_interval = std::chrono::milliseconds(200);
    config_val.expand_factor = 0.7;
    config_val.remove_factor = 0.2;
    config_val.task_limit = 12;
    config_val.avg_task_threshold = 1.5;
    config_val.idle_time_threshold = std::chrono::milliseconds(1500);
    config_val.consecutive_idle_count = 5;
    config_val.enable_dynamic_thread = false;

    nlohmann::json j = config_val;

    ofstream output_file(CONFIG_TO_JSON, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(output_file.is_open()) << "打开输出文件失败";
    output_file << j;
    output_file.close();
    
    // 下列开始测试将

}

/**
 * @brief 测试将JSON文件中的配置参数先转成std::string，再转成配置Config结构体与QueueFullPolicy枚举类
 */
TEST_F(JSONConvertion, JSON2Config) {
    const fs::path JSON_TO_CONFIG = CONFIG_DIR / "JSON_to_config.json";

    Config config = threadpool::loadConfigFromFile(JSON_TO_CONFIG.string());
    
    //检查已设定的值
    ASSERT_EQ(config.core_threads, 4) << "核心线程数应与文件中的值保持一致";
    ASSERT_EQ(config.max_threads, 8) << "最大线程数应与文件中的值保持一致";
    ASSERT_EQ(config.max_queue_size, 500) << "最大队列大小应与文件中的值保持一致";
    ASSERT_EQ(config.policy, QueueFullPolicy::BLOCKING) << "队满入队策略应与文件中的值保持一致";
    ASSERT_EQ(config.idle_time_threshold.count(), 500) << "线程空闲时间门槛应与文件中的值保持一致";
    
    // 检查默认值
    ASSERT_EQ(config.balancer_check_interval.count(), 500) << "平衡线程扫描间隔应与默认值保持一致";
    ASSERT_EQ(config.expand_factor, 0.8) << "载荷因子的增加线程门槛应与默认值保持一致";
    ASSERT_EQ(config.remove_factor, 0.3) << "载荷因子的移除线程门槛应与默认值保持一致";
    ASSERT_EQ(config.task_limit, 24) << "任务阈值应与默认值保持一致";
    ASSERT_EQ(config.avg_task_threshold, 2.0) << "任务/线程阈值应与默认值保持一致";
    ASSERT_EQ(config.consecutive_idle_count, 3) << "线程连续空闲次数门槛应与默认值保持一致";
    ASSERT_EQ(config.enable_dynamic_thread, true) << "启用动态线程布尔值应与默认值保持一致";
}

/**
 * @brief 测试将LoggerConfig写入JSON文件
 */
TEST_F(JSONConvertion, LoggerConfig2JSON) {
    nlohmann::json j = LoggerConfig();

    ofstream output_file(LOGGERCONFIG_TO_JSON, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(output_file.is_open()) << "打开输出文件失败";
    output_file << j;
    output_file.close();
}

/**
 * @brief 测试从JSON文件读入LoggerConfig结构体
 */
TEST_F(JSONConvertion, JSON2LoggerConfig) {
    LoggerConfig config = threadpool::loadLoggerConfigFromFile(JSON_TO_LOGGERCONFIG.string());

    ASSERT_EQ(config.name, "test_name");
    ASSERT_EQ(config.file_path, "./logs/thread_pool.log");
    ASSERT_EQ(config.level, threadpool::LogLevel::DEBUG);
    ASSERT_TRUE(config.enable_console);
    ASSERT_FALSE(config.enable_file);
    ASSERT_EQ(config.max_file_size, 10485760);
    ASSERT_EQ(config.max_files, 20);
    ASSERT_FALSE(config.auto_flush);
}

/**
 * @brief 测试利用配置结构体构建线程池的分类
 */
class CreateFromConfig : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    const fs::path CONFIG_PATH = fs::current_path() / ".." / ".." / "config" / "config.json";
    /**
     * 配置文件内有如下配置
     * "core_threads":4,
     * "max_threads":8,
     * "max_queue_size":10000,
     * "queue_full_policy":"BLOCKING",
     * "balancer_check_interval_ms":200,
     * "expand_factor":0.7,
     * "remove_factor":0.3,
     * "task_limit":24,
     * "avg_task_threshold":3.0,
     * "idle_time_threshold_ms":1500,
     * "consecutive_idle_count":5,
     * "enable_dynamic_thread":true
    */
};

/**
 * @brief 检查能否直接利用Config构造线程池，以及所构造的线程池的行为是否与预期相符
 */
TEST_F(CreateFromConfig, FromCTOR) {
    ASSERT_TRUE(fs::exists(CONFIG_PATH));
    Config config;
    ASSERT_NO_THROW(config = threadpool::loadConfigFromFile(CONFIG_PATH.string()));
    ThreadPool thread_pool(config);
    for (int i = 0; i < 4; i++) {
        thread_pool.submitWithResult([]() {
            sleep_for(milliseconds(100));
            return 1;
        });
    }
    ASSERT_TRUE(thread_pool.isRunning());

    thread_pool.shutdown(ThreadPool::ShutdownOption::WAIT_ALL);
    ThreadPool::Status status = thread_pool.getStatus();

    ASSERT_EQ(thread_pool.maxThreadCount(), 8);
    ASSERT_EQ(thread_pool.coreThreadCount(), 4);
    ASSERT_EQ(thread_pool.getQueueFullPolicy(), QueueFullPolicy::BLOCKING);
    ASSERT_EQ(status.tasks_succeeded, 4);
    ASSERT_GE(status.thread_created, 4);
    ASSERT_EQ(status.thread_destroyed, 0);
}

/**
 * @brief 测试利用makeThreadPool函数构造线程池
 */
TEST_F(CreateFromConfig, FromMakeFunction) {
    ASSERT_TRUE(fs::exists(CONFIG_PATH));
    std::unique_ptr<ThreadPool> thread_pool = threadpool::makeThreadPool(CONFIG_PATH.string());
    ASSERT_TRUE(thread_pool->isRunning());
    thread_pool->shutdown(ThreadPool::ShutdownOption::FORCE);
    ASSERT_TRUE(thread_pool->isStopped());

    Config config;
    ASSERT_NO_THROW(thread_pool = threadpool::makeThreadPool(config));
    ASSERT_TRUE(thread_pool->isRunning());
    thread_pool->shutdown(ThreadPool::ShutdownOption::FORCE);
    ASSERT_TRUE(thread_pool->isStopped());
}

int main(int argc, char **argv) {
    // Windows环境下防止输出中文乱码
    #ifdef _WIN32
        system("chcp 65001");
    #endif
    if (!fs::exists(CONFIG_DIR)) {
        fs::create_directories(CONFIG_DIR);
    }
    createJSONFile();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}