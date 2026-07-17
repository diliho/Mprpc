#include "ratelimiter.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <cstdlib>

RateLimiter::RateLimiter(std::shared_ptr<RedisClient> redis,
                         int max_requests,
                         int window_sec,
                         RateLimitAlgorithm algorithm)
    : m_redis(redis)
    , m_max_requests(max_requests)
    , m_window_sec(window_sec)
    , m_algorithm(algorithm)
{
    LOG_INFO("RateLimiter created: max=%d, window=%ds, algo=%d",
             max_requests, window_sec, static_cast<int>(algorithm));
}

void RateLimiter::UpdateRules(const RateLimitConfig& config) {
    m_max_requests.store(config.max_requests);
    m_window_sec.store(config.window_sec);
    m_algorithm.store(config.algorithm);
    LOG_INFO("RateLimiter rules updated: max=%d, window=%ds, algo=%d",
             config.max_requests, config.window_sec, static_cast<int>(config.algorithm));
}

std::string RateLimiter::buildKey(const std::string& service_name,
                                   const std::string& method_name,
                                   const std::string& client_ip,
                                   const std::string& suffix)
{
    std::ostringstream oss;
    oss << KEY_PREFIX << service_name << ":" << method_name;
    if (!client_ip.empty()) oss << ":" << client_ip;
    if (!suffix.empty()) oss << ":" << suffix;
    return oss.str();
}

bool RateLimiter::allow(const std::string& service_name,
                        const std::string& method_name,
                        const std::string& client_ip)
{
    if (!m_redis || !m_redis->IsConnected())
    {
        return true;
    }

    switch (m_algorithm.load())
    {
    case RateLimitAlgorithm::FIXED_WINDOW:
        return allowFixedWindow(buildKey(service_name, method_name, client_ip, "fixed"));
    case RateLimitAlgorithm::SLIDING_WINDOW_LOG:
        return allowSlidingWindowLog(buildKey(service_name, method_name, client_ip, "sliding"));
    case RateLimitAlgorithm::SLIDING_WINDOW_COUNTER:
        return allowSlidingWindowCounter(buildKey(service_name, method_name, client_ip));
    case RateLimitAlgorithm::TOKEN_BUCKET:
        return allowTokenBucket(buildKey(service_name, method_name, client_ip, "token"));
    case RateLimitAlgorithm::LEAKY_BUCKET:
        return allowLeakyBucket(buildKey(service_name, method_name, client_ip, "leaky"));
    }
    return true;
}

bool RateLimiter::allowFixedWindow(const std::string& key)
{
    int64_t count = m_redis->Incr(key);
    if (count == 1)
    {
        m_redis->Expire(key, m_window_sec.load());
    }
    return count <= m_max_requests.load();
}

bool RateLimiter::allowSlidingWindowLog(const std::string& key)
{
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int window_sec = m_window_sec.load();
    int64_t window_start = now - window_sec * 1000;

    const char* script =
        "redis.call('ZREMRANGEBYSCORE', KEYS[1], 0, ARGV[1]) "
        "local count = redis.call('ZCARD', KEYS[1]) "
        "if count < tonumber(ARGV[2]) then "
        "    redis.call('ZADD', KEYS[1], ARGV[3], ARGV[4]) "
        "    redis.call('EXPIRE', KEYS[1], ARGV[5]) "
        "    return 1 "
        "else "
        "    return 0 "
        "end";

    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {
        std::to_string(window_start),
        std::to_string(m_max_requests.load()),
        std::to_string(now),
        std::to_string(now) + ":" + std::to_string(rand()),
        std::to_string(window_sec + 1)
    };

    std::string result = m_redis->Eval(script, keys, args);
    return result == "1";
}

bool RateLimiter::allowSlidingWindowCounter(const std::string& key)
{
    int window_sec = m_window_sec.load();
    int sub_window_ms = (window_sec * 1000) / 10;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int64_t current_sub = now / sub_window_ms;
    std::string current_key = key + ":sub:" + std::to_string(current_sub);

    int64_t current_count = m_redis->Incr(current_key);
    if (current_count == 1)
    {
        m_redis->Expire(current_key, window_sec * 2);
    }

    int64_t total = 0;
    int64_t oldest_sub = current_sub - 10;
    for (int64_t i = current_sub; i > oldest_sub; --i)
    {
        std::string sub_key = key + ":sub:" + std::to_string(i);
        std::string val = m_redis->Get(sub_key);
        if (!val.empty())
        {
            total += std::stoll(val);
        }
    }

    return total <= m_max_requests.load();
}

bool RateLimiter::allowTokenBucket(const std::string& key)
{
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int max_req = m_max_requests.load();
    int window_sec = m_window_sec.load();
    int capacity = max_req;
    double rate = static_cast<double>(max_req) / window_sec;

    const char* script =
        "local key = KEYS[1] "
        "local capacity = tonumber(ARGV[1]) "
        "local rate = tonumber(ARGV[2]) "
        "local now = tonumber(ARGV[3]) "
        "local bucket = redis.call('HMGET', key, 'tokens', 'last_refill') "
        "local tokens = tonumber(bucket[1] or capacity) "
        "local last_refill = tonumber(bucket[2] or now) "
        "local elapsed = math.max(0, now - last_refill) "
        "local new_tokens = math.min(capacity, tokens + elapsed * rate / 1000) "
        "if new_tokens >= 1 then "
        "    redis.call('HMSET', key, 'tokens', new_tokens - 1, 'last_refill', now) "
        "    redis.call('EXPIRE', key, 86400) "
        "    return 1 "
        "else "
        "    redis.call('HMSET', key, 'tokens', new_tokens, 'last_refill', now) "
        "    return 0 "
        "end";

    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {
        std::to_string(capacity),
        std::to_string(rate),
        std::to_string(now_ms)
    };

    std::string result = m_redis->Eval(script, keys, args);
    return result == "1";
}

bool RateLimiter::allowLeakyBucket(const std::string& key)
{
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const char* script =
        "local key = KEYS[1] "
        "local capacity = tonumber(ARGV[1]) "
        "local now = tonumber(ARGV[2]) "
        "local current = redis.call('LLEN', key) "
        "if current >= capacity then "
        "    return 0 "
        "end "
        "redis.call('RPUSH', key, now) "
        "redis.call('EXPIRE', key, 3600) "
        "return 1 ";

    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {
        std::to_string(m_max_requests.load()),
        std::to_string(now_ms)
    };

    std::string result = m_redis->Eval(script, keys, args);
    return result == "1";
}

int64_t RateLimiter::getCurrentCount(const std::string& service_name,
                                     const std::string& method_name,
                                     const std::string& client_ip)
{
    if (!m_redis || !m_redis->IsConnected()) return 0;

    std::string key = buildKey(service_name, method_name, client_ip, "fixed");
    std::string val = m_redis->Get(key);
    return val.empty() ? 0 : std::stoll(val);
}

void RateLimiter::setMaxRequests(int max_requests)
{
    m_max_requests.store(max_requests);
    LOG_INFO("RateLimiter max_requests updated to %d", max_requests);
}

void RateLimiter::setWindowSec(int window_sec)
{
    m_window_sec.store(window_sec);
    LOG_INFO("RateLimiter window_sec updated to %d", window_sec);
}

void RateLimiter::setAlgorithm(RateLimitAlgorithm algorithm)
{
    m_algorithm.store(algorithm);
    LOG_INFO("RateLimiter algorithm updated to %d", static_cast<int>(algorithm));
}
