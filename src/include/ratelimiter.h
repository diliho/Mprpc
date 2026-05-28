#pragma once
#include <string>
#include <memory>
#include "redisutil.h"

enum class RateLimitAlgorithm {
    FIXED_WINDOW = 0,
    SLIDING_WINDOW_LOG = 1,
    SLIDING_WINDOW_COUNTER = 2,
    TOKEN_BUCKET = 3,
    LEAKY_BUCKET = 4
};

class RateLimiter {
public:
    RateLimiter(std::shared_ptr<RedisClient> redis,
                int max_requests,
                int window_sec,
                RateLimitAlgorithm algorithm = RateLimitAlgorithm::TOKEN_BUCKET);

    ~RateLimiter() = default;

    bool allow(const std::string& service_name,
               const std::string& method_name,
               const std::string& client_ip = "");

    int64_t getCurrentCount(const std::string& service_name,
                            const std::string& method_name,
                            const std::string& client_ip = "");

    void setMaxRequests(int max_requests);
    void setWindowSec(int window_sec);

    static constexpr const char* KEY_PREFIX = "mprpc:ratelimit:";

private:
    std::shared_ptr<RedisClient> m_redis;
    int m_max_requests;
    int m_window_sec;
    RateLimitAlgorithm m_algorithm;

    std::string buildKey(const std::string& service_name,
                         const std::string& method_name,
                         const std::string& client_ip,
                         const std::string& suffix = "");

    bool allowFixedWindow(const std::string& key);
    bool allowSlidingWindowLog(const std::string& key);
    bool allowSlidingWindowCounter(const std::string& key);
    bool allowTokenBucket(const std::string& key);
    bool allowLeakyBucket(const std::string& key);
};
