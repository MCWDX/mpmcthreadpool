#pragma once

#include <string>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace threadpool {

/** 线程池日志等级枚举类 */
enum class LogLevel {
    TRACE = 0,      // 最详细的跟踪信息，所有输出均记录到Console
    DEBUG = 1,      // 调试信息，开发与测试环境常用
    INFO = 2,       // 一般消息，记录程序正常运行状态
    WARN = 3,       // 警告信息，程序可继续运行但仍需注意
    ERR = 4,        // 错误信息，程序出错但可恢复，缩写成ERR是因为windows中ERROR宏定义成0了
    CRITICAL = 5,   // 严重错误，程序可能崩溃
    OFF = 6         // 关闭日志输出
};

// 线程池日志等级转换为spdlog日志等级与字符串的转换函数
spdlog::level::level_enum logLevelToSpdLogLevel(const LogLevel& level) noexcept;
std::string logLevelToString(const LogLevel& level) noexcept;
LogLevel stringToLogLevel(const std::string& level);

/**
 * 线程池日志记录器的配置结构体
 * 其中包含日志记录器名称，日志文件路径，日志模式，日志等级，是否启用控制台，
 * 是否记录到文件，文件最大大小，最大文件数，是否自动flush等配置信息
 */
struct LoggerConfig {
    std::string name = "thread_pool";                           // 日志记录器名称
    std::string file_path = "./../logs/thread_pool.log";        // 日志文件存放位置
    std::string pattern = "[%Y-%m-%d %T.%e] [%^%l%$] [%n] %v";  // 日志格式
    LogLevel level = LogLevel::INFO;                            // 日志等级
    bool enable_console = true;                                 // 是否启用控制台显示日志
    bool enable_file = true;                                    // 是否启用日志文件
    std::size_t max_file_size = 10 * 1024 * 1024;               // 日志文件最大文件大小，单位为字节数
    std::size_t max_files = 5;                                  // 日志文件最大文件数
    bool auto_flush = true;                                     // 是否启用自动刷新缓冲区
};

// 加载LoggerConfig所用函数
LoggerConfig loadLoggerConfigFromFile(const std::string& config_path);
LoggerConfig loadLoggerConfigFromString(const std::string& config_str);

/**
 * 日志记录器类，采用单例模式，包含以下方法
 * - getInstance()获取单例的引用
 * - initialized()初始化记录器与isInitialized()判断是否已初始化
 * - shutdown()关闭当前记录器
 * - createLogDirectory()创建日志文件夹
 * - trace, debug, info, warn, error, critical共6种日志记录等级
 */
class Logger {
public:

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static Logger& getInstance();

    bool initialize(const LoggerConfig& config);
    bool isInitialized() { return initialized_; };
    bool shutdown();
    
    bool createLogDirectory(const std::string& file_path);

    template<typename... Args>
    void trace(const std::string& fmt, Args&&... args);
    template<typename... Args>
    void debug(const std::string& fmt, Args&&... args);
    template<typename... Args>
    void info(const std::string& fmt, Args&&... args);
    template<typename... Args>
    void warn(const std::string& fmt, Args&&... args);
    template<typename... Args>
    void error(const std::string& fmt, Args&&... args);
    template<typename... Args>
    void critical(const std::string& fmt, Args&&... args);

private:
    Logger() = default;
    ~Logger();

    std::shared_ptr<spdlog::logger> logger_;
    LoggerConfig config_;
    bool initialized_{false};
};

/**
 * @brief 记录trace级别的信息
 */
template<typename... Args>
void Logger::trace(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::trace)) {
        logger_->trace(fmt, std::forward<Args>(args)...);
    }
}

/**
 * @brief 记录debug级别的信息
 */
template<typename... Args>
void Logger::debug(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::debug)) {
        logger_->debug(fmt, std::forward<Args>(args)...);
    }
}

/**
 * @brief 记录info级别的信息
 */
template<typename... Args>
void Logger::info(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::info)) {
        logger_->info(fmt, std::forward<Args>(args)...);
    }
}

/**
 * @brief 记录warn级别的信息
 */
template<typename... Args>
void Logger::warn(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::warn)) {
        logger_->warn(fmt, std::forward<Args>(args)...);
    }
}

/**
 * @brief 记录error级别的信息
 */
template<typename... Args>
void Logger::error(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::err)) {
        logger_->error(fmt, std::forward<Args>(args)...);
    }
}

/**
 * @brief 记录critical级别的信息
 */
template<typename... Args>
void Logger::critical(const std::string& fmt, Args&&... args) {
    if (logger_ && logger_->should_log(spdlog::level::critical)) {
        logger_->critical(fmt, std::forward<Args>(args)...);
    }
}

} // namespace threadpool

/** 获取记录器实例同时记录trace级别信息 */
#define LOG_TRACE(fmt, ...) threadpool::Logger::getInstance().trace(fmt, ##__VA_ARGS__)

/** 获取记录器实例同时记录debug级别信息 */
#define LOG_DEBUG(fmt, ...) threadpool::Logger::getInstance().debug(fmt, ##__VA_ARGS__)

/** 获取记录器实例同时记录info级别信息 */
#define LOG_INFO(fmt, ...) threadpool::Logger::getInstance().info(fmt, ##__VA_ARGS__)

/** 获取记录器实例同时记录warn级别信息 */
#define LOG_WARN(fmt, ...) threadpool::Logger::getInstance().warn(fmt, ##__VA_ARGS__)

/** 获取记录器实例同时记录error级别信息 */
#define LOG_ERROR(fmt, ...) threadpool::Logger::getInstance().error(fmt, ##__VA_ARGS__)

/** 获取记录器实例同时记录critical级别信息 */
#define LOG_CRITICAL(fmt, ...) threadpool::Logger::getInstance().critical(fmt, ##__VA_ARGS__)

/** 根据条件，记录对应级别的日志信息 */
#define LOG_IF(condition, level, fmt, ...) \
    do { \
        if (condition) { \
            LOG_##level(fmt, ##__VA_ARGS__); \
        }\
    } while (0)

namespace nlohmann {

template<>
struct adl_serializer<threadpool::LogLevel> {
    static void to_json(json&j, const threadpool::LogLevel& level) {
        j = threadpool::logLevelToString(level);
    }

    static void from_json(const json&j, threadpool::LogLevel& level) {
        level = threadpool::stringToLogLevel(j.get<std::string>());
    }
};

template<>
struct adl_serializer<threadpool::LoggerConfig> {
    static void to_json(json&j, const threadpool::LoggerConfig& config) {
        j = json{
            {"name", config.name},
            {"file_path", config.file_path},
            {"pattern", config.pattern},
            {"level", config.level},
            {"enable_console", config.enable_console},
            {"enable_file", config.enable_file},
            {"max_file_size", config.max_file_size},
            {"max_files", config.max_files},
            {"auto_flush", config.auto_flush}
        };
    }

    static void from_json(const json& j, threadpool::LoggerConfig& config) {
        if (j.contains("name")) {
            config.name = j.at("name").get<std::string>();
        }
        if (j.contains("file_path")) {
            config.file_path = j.at("file_path").get<std::string>();
        }
        if (j.contains("pattern")) {
            config.pattern = j.at("pattern").get<std::string>();
        }
        if (j.contains("level")) {
            config.level = j.at("level").get<threadpool::LogLevel>();
        }
        if (j.contains("enable_console")) {
            config.enable_console = j.at("enable_console").get<bool>();
        }
        if (j.contains("enable_file")) {
            config.enable_file = j.at("enable_file").get<bool>();
        }
        if (j.contains("max_file_size")) {
            config.max_file_size = j.at("max_file_size").get<std::size_t>();
        }
        if (j.contains("max_files")) {
            config.max_files = j.at("max_files").get<std::size_t>();
        }
        if (j.contains("auto_flush")) {
            config.auto_flush = j.at("auto_flush").get<bool>();
        }
    }
};

} // namespace nlohmann