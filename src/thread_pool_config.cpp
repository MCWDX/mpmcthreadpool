#include "thread_pool_config.h"

#include <iostream>
#include <fstream>
#include <filesystem>

using std::cout;
using std::endl;
namespace fs = std::filesystem;

namespace threadpool{
/**
 * @brief 将QueueFullPolicy转换成std::string类型
 * @param policy 满队入队策略
 */
std::string policyToString(QueueFullPolicy policy) {
    switch (policy) {
        case QueueFullPolicy::BLOCKING:
            return "BLOCKING";
        case QueueFullPolicy::DISCARD:
            return "DISCARD";
        case QueueFullPolicy::OVERWRITE:
            return "OVERWRITE";
        default:
            throw std::invalid_argument("Unknown queue full policy value");
    }
}

/**
 * @brief 将满队入队策略字符串转换成QueueFullPolicy枚举类，大小写不敏感
 * @param policy_str 满队入队策略，必须为"BLOCKING", "DISCARD", "OVERWRITE"其中之一
 */
QueueFullPolicy stringToPolicy(const std::string& policy_str) {
    // 统一转化为大写
    std::string upper_policy = policy_str;
    std::transform(upper_policy.begin(), upper_policy.end(), upper_policy.begin(), ::toupper);
    if (upper_policy == "BLOCKING") {
        return QueueFullPolicy::BLOCKING;
    } else if (upper_policy == "DISCARD") {
        return QueueFullPolicy::DISCARD;
    } else if (upper_policy == "OVERWRITE") {
        return QueueFullPolicy::OVERWRITE;
    } else {
        throw std::invalid_argument("Unknown queue full policy string");
    }
}

/**
 * @brief 从文件中读取线程池设置参数，文件需为json格式
 * @param config_path 设置文件路径
 */
Config loadConfigFromFile(const std::string& config_path) {
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
    return loadConfigFromString(json_content);
}

/**
 * @brief 从配置字符串中加载配置参数构建配置结构体
 * @param config_str 配置字符串
 */
Config loadConfigFromString(const std::string& config_str) {
    try {
        nlohmann::json j = nlohmann::json::parse(config_str);        
        Config config = j.get<Config>();

        validateConfigBasic(config);
        validateConfigExtra(config);

        return config;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument("Json parsing error");
    } catch (const nlohmann::json::exception& e) {
        throw std::invalid_argument("Json exception occured");
    }
}

/**
 * @brief 检查所提供的config是否满足线程池运行的必要条件
 * @param config 待验证线程池配置
 * @throw 所提供的config不能支持线程池运行时，抛出std::invalid_argument
 */
void validateConfigBasic(const threadpool::Config& config) {
    // 线程池必须有线程
    if (config.core_threads == 0) {
        throw std::invalid_argument("Num of core threads must be GREATER THAN 0");
    }

    // 最大线程数不能少于核心线程数
    if (config.max_threads < config.core_threads) {
        throw std::invalid_argument("Num of max thread can't be LESS THAN num of core thread");
    }

    // 不允许任务队列为空
    if (config.max_queue_size == 0) {
        throw std::invalid_argument("max task queue size must be GREATER THAN 0");
    }

    if (config.enable_dynamic_thread) {
        // 增加线程门槛值应在(0.0, 1.0]范围内(0.0需留给移除门槛)
        if (config.expand_factor <= 0.0 || config.expand_factor > 1.0) {
            throw std::invalid_argument("Expand factor must be in range of (0.0, 1.0]");
        }

        // 移除线程门槛应在[0.0, 增加线程门槛值)的范围内
        if (config.remove_factor < 0.0 || config.remove_factor >= config.expand_factor) {
            throw std::invalid_argument("Remove factor must be in range of [0.0, expand factor)");
        }

        // 连续空闲次数门槛不应为0，至少要大于0
        if (config.consecutive_idle_count == 0) {
            throw std::invalid_argument("Consecutive idle count threshold should be greater than 0");
        }
    }
    
}

/**
 * @brief 进行额外检查，不抛出异常，仅对提醒部分参数可能影响性能
 */
void validateConfigExtra(const threadpool::Config& config) {

    const std::size_t  recommended_max = std::thread::hardware_concurrency() * 4;
    if (config.max_threads > recommended_max) {
        std::cerr << "Warning: max_thread = " << config.max_threads << " is greater than recommended max num = " << 
            recommended_max << ". May lead to frequent thread switching" << std::endl;
    }

    if (config.max_queue_size > 100000) {
        std::cerr << "Warning: max queue size " << config.max_queue_size << " is too high, " << 
            "may consume too much memory" << std::endl;
    }

    if (config.enable_dynamic_thread) {
        if (config.task_limit <= 5) {
            std::cerr << "Warning: Task limit lower than or equal to 5, may frequently create threads" << std::endl;
        }

        if (config.balancer_check_interval.count() < 10) {
            std::cerr << "Warning: Interval between consecutive checks is too short, may frequently manage threads" << 
                std::endl;
        }

        if (config.idle_time_threshold.count() < 10) {
            std::cerr << "Warning: Thread idle time threshold is too low, may frequently remove threads" << std::endl;
        }
    }
}

} // namespace threadpool