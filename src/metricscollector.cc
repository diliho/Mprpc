#include "metricscollector.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstdlib>

// Lua script: INCR + EXPIRE in one atomic round trip
static const char* kIncrExpireScript =
    "redis.call('INCR', KEYS[1])\n"
    "redis.call('EXPIRE', KEYS[1], ARGV[1])";

MetricsCollector::MetricsCollector(std::shared_ptr<RedisClient> redis)
    : m_redis(redis)
{
    LOG_INFO("MetricsCollector initialized");
}

std::string MetricsCollector::currentMinuteKey()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm result;
    localtime_r(&tt, &result);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d",
             result.tm_year + 1900, result.tm_mon + 1, result.tm_mday,
             result.tm_hour, result.tm_min);
    return buf;
}

static void incrWithExpire(const std::shared_ptr<RedisClient>& redis,
                           const std::string& key, int ttl)
{
    if (!redis) return;
    std::vector<std::string> keys = {key};
    std::vector<std::string> args = {std::to_string(ttl)};
    redis->Eval(kIncrExpireScript, keys, args);
}

void MetricsCollector::recordCall(const std::string& service_name,
                                  const std::string& method_name,
                                  int64_t latency_ms,
                                  bool is_error)
{
    if (!m_redis) return;

    std::string method_path = "/" + service_name + "/" + method_name;
    std::string minute = currentMinuteKey();

    std::string calls_key = "mprpc:metrics" + method_path + ":calls:" + minute;
    incrWithExpire(m_redis, calls_key, 3600);

    if (is_error)
    {
        std::string err_key = "mprpc:metrics" + method_path + ":errors:" + minute;
        incrWithExpire(m_redis, err_key, 3600);
    }

    if (rand() % 10 == 0)
    {
        recordLatency(method_path, latency_ms);
    }
}

void MetricsCollector::recordLatency(const std::string& method_path, int64_t latency_ms)
{
    std::string key = "mprpc:metrics" + method_path + ":latency";

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_redis->ZAdd(key, static_cast<double>(latency_ms),
                  std::to_string(now_ms) + ":" + std::to_string(rand()));

    m_redis->ZRemRangeByRank(key, 0, -1001);
    m_redis->Expire(key, 3600);
}

void MetricsCollector::recordBlocked(const std::string& service_name,
                                     const std::string& method_name)
{
    if (!m_redis) return;

    std::string method_path = "/" + service_name + "/" + method_name;
    std::string minute = currentMinuteKey();

    std::string key = "mprpc:metrics" + method_path + ":blocked:" + minute;
    incrWithExpire(m_redis, key, 3600);
}
