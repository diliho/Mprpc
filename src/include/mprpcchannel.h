#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "logger.h"
#include "zookeeperutil.h"
#include "redisutil.h"
#include "ratelimiter.h"
#include "metricscollector.h"
#include "balance/load_balancer.h"
#include "net/connection_pool.h"
#include "circuit_breaker.h"
#include "registry/zk_registry.h"
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

    void setDirectAddress(const std::string& addr) { m_direct_addr = addr; }
    std::string getLastProvider() const { return m_last_provider; }

private:
    std::shared_ptr<mprpc::ZKRegistry> m_registry;
    bool m_zk_available;
    std::shared_ptr<RedisClient> m_redis;
    std::unique_ptr<MetricsCollector> m_metrics;
    std::unique_ptr<RateLimiter> m_client_rate_limiter;

    // key: "method_path" method-level breaker (sliding window)
    std::unordered_map<std::string, mprpc::SlidingWindowCircuitBreaker> m_method_breakers;
    std::mutex m_cb_mtx;

    // Instance cache: method_path → list of available instances (avoids ZK query on every call)
    std::unordered_map<std::string, std::vector<mprpc::ServiceInstance>> m_inst_cache;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_cache_timestamps;
    static constexpr int CACHE_TTL_SEC = 30;
    std::mutex m_cache_mtx;

    // Connection pools: key="ip:port" → ConnectionPool
    std::unordered_map<std::string, std::unique_ptr<mprpc::ConnectionPool>> m_conn_pools;
    std::mutex m_pool_mtx;

    static constexpr int MAX_RETRY = 3;

    // ─── ZK Watcher for real-time service up/down awareness ───
    static void serviceWatcher(zhandle_t* zh, int type, int state,
                                const char* path, void* watcherCtx);
    void invalidateCache(const std::string& method_path);
    void registerWatcher(const std::string& method_path);
    std::unordered_map<std::string, bool> m_watcher_registered;

    std::string discoverService(const std::string& service_name,
                                 const std::string& method_name);
    bool connectWithTimeout(int clientfd, const struct sockaddr_in& addr, int timeout_sec);
    void parseAndConnect(const std::string& host_data,
                         struct sockaddr_in& server_addr,
                         std::string& ip, uint16_t& port);

    std::unique_ptr<mprpc::LoadBalancer> m_balancer;
    std::mutex m_lb_mtx;

    std::string m_direct_addr;
    std::string m_last_provider;
};
