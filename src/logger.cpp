#include "logger.h"

#include <iostream>
#include <filesystem>
#include <fstream>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

using std::cerr;
using std::endl;
using std::string;
namespace fs = std::filesystem;

namespace threadpool {
/**
 * @brief 将LogLevel枚举类转换成spdlog的level
 * @param level LogLevel枚举类
 */
spdlog::level::level_enum logLevelToSpdLogLevel(const LogLevel& level) noexcept {
    switch(level) {
        case LogLevel::TRACE:    return spdlog::level::trace;
        case LogLevel::DEBUG:    return spdlog::level::debug;
        case LogLevel::INFO:     return spdlog::level::info;
        case LogLevel::WARN:     return spdlog::level::warn;
        case LogLevel::ERR:      return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        case LogLevel::OFF:      return spdlog::level::off;
        default:                 return spdlog::level::info;
    }
}

/**
 * @brief 将LogLevel枚举类转换成string类型
 * @param level LogLevel枚举类
 */
string logLevelToString(const LogLevel& level) noexcept {
    switch(level) {
        case LogLevel::TRACE:    return "trace";
        case LogLevel::DEBUG:    return "debug";
        case LogLevel::INFO:     return "info";
        case LogLevel::WARN:     return "warn";
        case LogLevel::ERR:      return "err";
        case LogLevel::CRITICAL: return "critical";
        case LogLevel::OFF:      return "off";
        default:                 return "info";
    }
}

/**
 * @brief 将string类型转换成LogLevel枚举类
 * @param level string类型的日志等级
 */
LogLevel stringToLogLevel(const string& level) {
    string upper_level = level;
    std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(), ::toupper);
    if (upper_level == "TRACE") {
        return LogLevel::TRACE;
    } else if (upper_level == "DEBUG") {
        return LogLevel::DEBUG;
    } else if (upper_level == "INFO") {
        return LogLevel::INFO;
    } else if (upper_level == "WARN") {
        return LogLevel::WARN;
    } else if (upper_level == "ERR") {
        return LogLevel::ERR;
    } else if (upper_level == "CRITICAL") {
        return LogLevel::CRITICAL;
    } else if (upper_level == "OFF") {
        return LogLevel::OFF;
    } else {
        cerr << "Invalid log level: " << level << ", using default level: INFO" << endl;
        return LogLevel::INFO;
    }
}

LoggerConfig loadLoggerConfigFromFile(const string& config_path) {
    if (!fs::exists(config_path)) {
        throw std::runtime_error("Config at " + config_path + " doesn't exist");
    } 

    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        throw std::runtime_error("Failed to open config file at " + config_path);
    }

    std::string json_content((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
    config_file.close();

    std::cout << "Config loaded, content length: " << json_content.length() << " bytes" << std::endl;
    return loadLoggerConfigFromString(json_content);
}

LoggerConfig loadLoggerConfigFromString(const string& config_str) {
    try {
        return nlohmann::json::parse(config_str).get<LoggerConfig>();
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument("Json parsing error");
    } catch (const nlohmann::json::exception& e) {
        throw std::invalid_argument("Json exception occured");
    }
}

/**
 * @brief 析构函数内不能调用spdlog::drop()与spdlog::shutdown()，否则会程序崩溃
 */
Logger::~Logger() {
    if (!initialized_) {
        return;
    }
    try {
        if (logger_) {
            logger_->info("Shutting down log system");
            logger_->flush();
            logger_.reset();
        }
        initialized_ = false;
    } catch (const std::exception& e) {
        cerr << "Failed to shutdown log system: " << e.what() << endl;
    }
}

/**
 * @return 以引用形式获取Logger单例
 */
Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

/**
 * @brief 初始化日志系统
 * @param config LoggerConfig实例，存储日志系统参数
 * @return 是否初始化成功，已初始化直接返回true
 */
bool Logger::initialize(const LoggerConfig& config) {
    // 已初始化则无需再次初始化
    if (initialized_) {
        cerr << "Logger is already initialized, don't initialize again" << endl;
        return true;
    }
    config_ = config;

    try {
        std::vector<spdlog::sink_ptr> sinks;

        // 控制台输出sink设置
        if (config_.enable_console) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(logLevelToSpdLogLevel(config_.level));
            console_sink->set_pattern(config_.pattern);
            sinks.push_back(console_sink);
        }

        // 文件输出sink设置
        if (config_.enable_file) {
            if (!createLogDirectory(config_.file_path)) {
                return false;
            }

            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config_.file_path,
                config_.max_file_size,
                config.max_files
            );
            file_sink->set_level(logLevelToSpdLogLevel(config_.level));
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        }
        
        // 构建logger
        logger_ = std::make_shared<spdlog::logger>(config_.name, sinks.begin(), sinks.end());
        logger_->set_level(logLevelToSpdLogLevel(config_.level));

        if (config_.auto_flush) {
            logger_->flush_on(spdlog::level::info);
        }

        spdlog::register_logger(logger_);
        spdlog::set_default_logger(logger_);

        // 确认已初始化，记录标记位与写入日志
        initialized_ = true;

        logger_->info("Log system initialized, name: {}, level: {}, console: {}, file: {}",
            config_.name,
            logLevelToString(config_.level),
            config_.enable_console ? "activated" : "disabled",
            config_.enable_file ? config_.file_path : "disabled"
        );

        return true;
    } catch (const std::exception& e) {
        cerr << "Error: failed to initialize log system, " << e.what() << endl;
        return false;
    }
}

/**
 * @brief 关停日志系统
 * @return 是否已关停日志系统(未初始化过就调用该函数也返回true)
 */
bool Logger::shutdown() {
    if (!initialized_) {
        return true;
    }

    try {
        if (logger_) {
            logger_->info("Shutting down log system");
            logger_->flush();
            spdlog::drop(config_.name);
            logger_.reset();
        }
        spdlog::shutdown();
        initialized_ = false;
    } catch (const std::exception& e) {
        cerr << "Failed to shutdown log system: " << e.what() << endl;
        return false;
    }
    return true;
}

/**
 * @brief 创建存储日志的文件夹
 * @return 是否创建成功
 */
bool Logger::createLogDirectory(const string& file_path) {
    try {
        fs::path path(file_path);
        fs::path dir = path.parent_path();

        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
            std::cout << "Creating log directory: " << dir << endl;
        }
        return true;

    } catch (const std::exception& e) {
        cerr << "Error: failed to create log directory" << endl;
        return false;
    }
}

} // namespace threadpool