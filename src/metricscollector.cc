#include "metricscollector.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <algorithm>

MetricsCollector::MetricsCollector(std::shared_ptr<RedisClient> redis, int ttl_sec)
    : m_redis(redis), m_ttl_sec(ttl_sec)
{
    m_current = new MinuteBucket();
    m_current->minute_key = currentMinuteKey();
    m_flush_thread = std::thread(&MetricsCollector::flushLoop, this);
    LOG_INFO("MetricsCollector initialized (ttl=%ds, flush_interval=%dms)",
             m_ttl_sec, FLUSH_INTERVAL_MS);
}

MetricsCollector::~MetricsCollector()
{
    m_running = false;
    if (m_flush_thread.joinable())
    {
        m_flush_thread.join();
    }

    MinuteBucket* last = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        last = m_current;
        m_current = nullptr;
    }
    if (last && !last->methods.empty())
    {
        flushBucket(last);
    }
    delete last;

    LOG_INFO("MetricsCollector destroyed");
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

void MetricsCollector::recordCall(const std::string& service_name,
                                  const std::string& method_name,
                                  int64_t latency_ms,
                                  bool is_error,
                                  bool is_timeout)
{
    std::string key = service_name + "/" + method_name;
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_current) return;

    MethodMetrics& m = m_current->methods[key];
    m.calls++;
    if (is_error) m.fails++;
    if (is_timeout) m.timeouts++;

    m.total_latency += latency_ms;
    if (latency_ms > m.max_latency) m.max_latency = latency_ms;
    if (latency_ms < m.min_latency) m.min_latency = latency_ms;

    if (m.latency_samples.size() < MAX_LATENCY_SAMPLES)
    {
        m.latency_samples.push_back(latency_ms);
    }
    else
    {
        size_t idx = rand() % (m.samples_seen + 1);
        if (idx < MAX_LATENCY_SAMPLES)
        {
            m.latency_samples[idx] = latency_ms;
        }
    }
    m.samples_seen++;
}

void MetricsCollector::recordBlocked(const std::string& service_name,
                                     const std::string& method_name)
{
    std::string key = service_name + "/" + method_name;
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_current) return;

    MethodMetrics& m = m_current->methods[key];
    m.blocked++;
}

void MetricsCollector::flushLoop()
{
    while (m_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(FLUSH_INTERVAL_MS));

        MinuteBucket* to_flush = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            if (!m_current) continue;

            std::string new_minute = currentMinuteKey();
            if (m_current->minute_key != new_minute)
            {
                to_flush = m_current;
                m_current = new MinuteBucket();
                m_current->minute_key = new_minute;
            }
            else if (!m_current->methods.empty())
            {
                to_flush = m_current;
                m_current = new MinuteBucket();
                m_current->minute_key = new_minute;
            }
        }

        if (to_flush)
        {
            flushBucket(to_flush);
            delete to_flush;
        }
    }
}

void MetricsCollector::flushBucket(MinuteBucket* bucket)
{
    if (!m_redis || bucket->methods.empty()) return;

    std::unordered_map<std::string, ServiceTotal> service_totals;

    for (auto& kv : bucket->methods)
    {
        const std::string& path = kv.first;
        MethodMetrics& mm = kv.second;

        size_t slash = path.find('/');
        if (slash == std::string::npos) continue;
        std::string svc = path.substr(0, slash);
        std::string method = path.substr(slash + 1);

        std::string key = "mprpc:metrics:" + svc + ":" + method + ":" + bucket->minute_key;

        if (mm.calls == 0) continue;

        m_redis->HSet(key, "calls", std::to_string(mm.calls));
        m_redis->HSet(key, "fails", std::to_string(mm.fails));
        m_redis->HSet(key, "timeouts", std::to_string(mm.timeouts));
        m_redis->HSet(key, "blocked", std::to_string(mm.blocked));
        m_redis->HSet(key, "time_avg", std::to_string(mm.total_latency / mm.calls));
        m_redis->HSet(key, "time_max", std::to_string(mm.max_latency));
        m_redis->HSet(key, "time_min", std::to_string(mm.min_latency));

        int64_t p99 = computePercentile(mm.latency_samples, 99.0);
        int64_t p95 = computePercentile(mm.latency_samples, 95.0);
        m_redis->HSet(key, "time_p99", std::to_string(p99));
        m_redis->HSet(key, "time_p95", std::to_string(p95));

        m_redis->Expire(key, m_ttl_sec);

        service_totals[svc].calls += mm.calls;
        service_totals[svc].fails += mm.fails;
        service_totals[svc].timeouts += mm.timeouts;
        service_totals[svc].blocked += mm.blocked;
    }

    for (auto& svc_kv : service_totals)
    {
        std::string svc_key = "mprpc:metrics:" + svc_kv.first + ":total:" + bucket->minute_key;
        m_redis->HSet(svc_key, "calls", std::to_string(svc_kv.second.calls));
        m_redis->HSet(svc_key, "fails", std::to_string(svc_kv.second.fails));
        m_redis->HSet(svc_key, "timeouts", std::to_string(svc_kv.second.timeouts));
        m_redis->HSet(svc_key, "blocked", std::to_string(svc_kv.second.blocked));
        m_redis->Expire(svc_key, m_ttl_sec);
    }
}

int64_t MetricsCollector::computePercentile(std::vector<int64_t>& samples, double p)
{
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    size_t idx = std::min(static_cast<size_t>(samples.size() * p / 100.0), samples.size() - 1);
    return samples[idx];
}
