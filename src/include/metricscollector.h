#pragma once
#include <memory>
#include <string>
#include "redisutil.h"

class MetricsCollector {
public:
    MetricsCollector(std::shared_ptr<RedisClient> redis);

    void recordCall(const std::string& service_name,
                    const std::string& method_name,
                    int64_t latency_ms,
                    bool is_error);

    void recordBlocked(const std::string& service_name,
                       const std::string& method_name);

    static std::string currentMinuteKey();

private:
    std::shared_ptr<RedisClient> m_redis;

    void recordLatency(const std::string& method_path, int64_t latency_ms);
};
