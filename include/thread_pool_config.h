#pragma once

#include <thread>

#include <nlohmann/json.hpp>

namespace threadpool {

/** 队列已满时的操作策略枚举类 */ 
enum class QueueFullPolicy { 
    BLOCKING,   // 阻塞等待入队
    OVERWRITE,  // 覆盖已有任务
    DISCARD     // 丢弃新任务
};

/**
 * 线程池配置结构体，包含线程数，队列大小，队满入队策略，动态线程管理等配置信息
 */
struct Config {
    std::size_t core_threads{4};                                    // 核心线程数
    std::size_t max_threads{std::thread::hardware_concurrency()};   // 最大线程数
    std::size_t max_queue_size{1000};                               // 最大队列大小
    QueueFullPolicy policy{QueueFullPolicy::BLOCKING};              // 默认队满入队策略

    std::chrono::milliseconds balancer_check_interval{500};         // 平衡线程扫描间隔
    double expand_factor{0.8};                                      // 载荷因子的增加线程门槛
    double remove_factor{0.3};                                      // 载荷因子的移除线程门槛
    std::size_t task_limit{24};                                     // 任务阈值，任务超过阈值时指示应增加线程数
    double avg_task_threshold{2.0};                                 // 任务/线程阈值，超过该阈值指示任务过多应增加线程
    std::chrono::milliseconds idle_time_threshold{2000};            // 线程空闲时间门槛，超过该时间则空闲计数+1
    std::size_t consecutive_idle_count{3};                          // 线程连续空闲次数门槛(空闲次数大于该值的线程应该被移除)
    bool enable_dynamic_thread{true};                               // 动态管理线程数标志位
};

// QueueFullPolicy与std::string互相转换函数
std::string policyToString(QueueFullPolicy policy);
QueueFullPolicy stringToPolicy(const std::string& policy_str);

// 加载Config所用函数
Config loadConfigFromFile(const std::string& config_path);
Config loadConfigFromString(const std::string& config_str);

// 验证config是否有效所用函数
void validateConfigBasic(const Config& config);
void validateConfigExtra(const Config& config);

} // namespace threadpool

namespace nlohmann {

template<>
struct adl_serializer<threadpool::QueueFullPolicy> {
    static void to_json(json& j, const threadpool::QueueFullPolicy& policy) {
        j = threadpool::policyToString(policy);
    }

    static void from_json(const json&j, threadpool::QueueFullPolicy& policy) {
        policy = threadpool::stringToPolicy(j.get<std::string>());
    }
};

template<>
struct adl_serializer<threadpool::Config> {
    static void to_json(json&j, const threadpool::Config& config) {
        j = json{
            {"core_threads", config.core_threads},
            {"max_threads", config.max_threads},
            {"max_queue_size", config.max_queue_size},
            {"queue_full_policy", config.policy},

            {"balancer_check_interval_ms", config.balancer_check_interval.count()},
            {"expand_factor", config.expand_factor},
            {"remove_factor", config.remove_factor},
            {"task_limit", config.task_limit},
            {"avg_task_threshold", config.avg_task_threshold},
            {"idle_time_threshold_ms", config.idle_time_threshold.count()},
            {"consecutive_idle_count", config.consecutive_idle_count},
            {"enable_dynamic_thread", config.enable_dynamic_thread}
        };
    }

    static void from_json(const json&j, threadpool::Config& config) {
        if (j.contains("core_threads")) {
            config.core_threads = j.at("core_threads").get<std::size_t>();
        }
        if (j.contains("max_threads")) {
            config.max_threads = j.at("max_threads").get<std::size_t>();
        }
        if (j.contains("max_queue_size")) {
            config.max_queue_size = j.at("max_queue_size").get<std::size_t>();
        }
        if (j.contains("queue_full_policy")) {
            config.policy = j.at("queue_full_policy").get<threadpool::QueueFullPolicy>();
        }
        
        //不带单位则默认单位为毫秒
        if (j.contains("balancer_check_interval")) {
            config.balancer_check_interval = 
                std::chrono::milliseconds(j.at("balancer_check_interval").get<int64_t>());
        } else {
            if (j.contains("balancer_check_interval_ms")) {
                config.balancer_check_interval = 
                    std::chrono::milliseconds(j.at("balancer_check_interval_ms").get<int64_t>());
            }
            if (j.contains("balancer_check_interval_seconds")) {
                config.balancer_check_interval = 
                    std::chrono::seconds(j.at("balancer_check_interval_seconds").get<int64_t>());
            }
        }
        if (j.contains("expand_factor")) {
            config.expand_factor = j.at("expand_factor").get<double>();
        }
        if (j.contains("remove_factor")) {
            config.remove_factor = j.at("remove_factor").get<double>();
        }
        if (j.contains("task_limit")) {
            config.task_limit = j.at("task_limit").get<std::size_t>();
        }
        if (j.contains("avg_task_threshold")) {
            config.avg_task_threshold = j.at("avg_task_threshold").get<double>();
        }

        // 不带单位就默认是毫秒
        if (j.contains("idle_time_threshold")) {
            config.idle_time_threshold = 
                std::chrono::milliseconds(j.at("idle_time_threshold").get<int64_t>());
        } else {
            if (j.contains("idle_time_threshold_ms")) {
                config.idle_time_threshold = 
                    std::chrono::milliseconds(j.at("idle_time_threshold_ms").get<int64_t>());
            }
            if (j.contains("idle_time_threshold_seconds")) {
                config.idle_time_threshold = 
                    std::chrono::seconds(j.at("idle_time_threshold_seconds").get<int64_t>());
            }
        }
        
        if (j.contains("consecutive_idle_count")) {
            config.consecutive_idle_count = j.at("consecutive_idle_count").get<std::size_t>();
        }
        if (j.contains("enable_dynamic_thread")) {
            config.enable_dynamic_thread = j.at("enable_dynamic_thread").get<bool>();
        }
    }
};

} // namespace nlohmann