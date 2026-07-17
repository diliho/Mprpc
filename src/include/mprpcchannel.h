#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "logger.h"
#include "zookeeperutil.h"
#include "redisutil.h"
#include "ratelimiter.h"
#include "metricscollector.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>

class MprpcChannel : public google::protobuf::RpcChannel
{
public:
    MprpcChannel();
    ~MprpcChannel();
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done);

private:
    std::shared_ptr<ZKClient> m_zkclient;
    bool m_zk_available;
    std::shared_ptr<RedisClient> m_redis;
    std::unique_ptr<MetricsCollector> m_metrics;
    std::unique_ptr<RateLimiter> m_client_rate_limiter;

    class CircuitBreaker {
    public:
        CircuitBreaker(int threshold = 5, int timeout_sec = 30);
        bool allowRequest();
        void onSuccess();
        void onFailure();
    private:
        enum class State { CLOSED, OPEN, HALF_OPEN };
        int m_threshold;
        int m_timeout_sec;
        int m_failure_count;
        State m_state;
        std::chrono::steady_clock::time_point m_last_failure_time;
    };

    // key: "method_path" method-level breaker
    std::unordered_map<std::string, CircuitBreaker> m_method_breakers;
    std::mutex m_cb_mtx;

    // Address cache: method_path → "ip:port" (avoids ZK query on every call)
    std::unordered_map<std::string, std::string> m_addr_cache;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_cache_timestamps;
    static constexpr int CACHE_TTL_SEC = 30;
    std::mutex m_cache_mtx;

    // Persistent TCP connections: key="method_path@ip:port" → fd
    std::unordered_map<std::string, int> m_persistent_fds;

    static constexpr int MAX_RETRY = 3;

    // ─── ZK Watcher for real-time service up/down awareness ───
    static void serviceWatcher(zhandle_t* zh, int type, int state,
                                const char* path, void* watcherCtx);
    void invalidateCache(const std::string& method_path);
    void registerWatcher(const std::string& method_path);
    std::unordered_map<std::string, bool> m_watcher_registered;

    std::string discoverService(const std::string& method_path);
    bool connectWithTimeout(int clientfd, const struct sockaddr_in& addr, int timeout_sec);
    void parseAndConnect(const std::string& host_data,
                         struct sockaddr_in& server_addr,
                         std::string& ip, uint16_t& port);
};
