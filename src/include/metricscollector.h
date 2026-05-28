#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <limits>
#include "redisutil.h"

class MetricsCollector {
public:
    MetricsCollector(std::shared_ptr<RedisClient> redis, int ttl_sec = 3600);
    ~MetricsCollector();

    void recordCall(const std::string& service_name,
                    const std::string& method_name,
                    int64_t latency_ms,
                    bool is_error,
                    bool is_timeout = false);

    void recordBlocked(const std::string& service_name,
                       const std::string& method_name);

    static std::string currentMinuteKey();

private:
    struct MethodMetrics {
        int64_t calls{0};
        int64_t fails{0};
        int64_t timeouts{0};
        int64_t blocked{0};
        int64_t total_latency{0};
        int64_t max_latency{0};
        int64_t min_latency{std::numeric_limits<int64_t>::max()};
        size_t samples_seen{0};
        std::vector<int64_t> latency_samples;
    };

    struct ServiceTotal {
        int64_t calls{0};
        int64_t fails{0};
        int64_t timeouts{0};
        int64_t blocked{0};
    };

    struct MinuteBucket {
        std::string minute_key;
        std::unordered_map<std::string, MethodMetrics> methods;
    };

    std::shared_ptr<RedisClient> m_redis;
    int m_ttl_sec;

    MinuteBucket* m_current;
    std::mutex m_mtx;

    std::thread m_flush_thread;
    std::atomic<bool> m_running{true};

    void flushLoop();
    void flushBucket(MinuteBucket* bucket);

    int64_t computePercentile(std::vector<int64_t>& samples, double p);

    static constexpr int FLUSH_INTERVAL_MS = 2000;
    static constexpr size_t MAX_LATENCY_SAMPLES = 4096;
};
