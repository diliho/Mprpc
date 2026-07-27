#include "mprpcchannel.h"
#include "rpcheader.pb.h"
#include "mprpcapplication.h"
#include "rpcerror.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include "mprpccontroller.h"
#include "logger.h"
#include "rpcerror.h"

// ─── CircuitBreaker ────────────────────────────────────────────

MprpcChannel::CircuitBreaker::CircuitBreaker(int threshold, int timeout_sec)
    : m_threshold(threshold), m_timeout_sec(timeout_sec), m_failure_count(0),
      m_state(State::CLOSED)
{}

bool MprpcChannel::CircuitBreaker::allowRequest()
{
    auto now = std::chrono::steady_clock::now();
    switch (m_state)
    {
    case State::CLOSED:
        return true;
    case State::OPEN:
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_last_failure_time).count();
        if (elapsed >= m_timeout_sec)
        {
            m_state = State::HALF_OPEN;
            LOG_INFO("Circuit breaker HALF_OPEN, probing...");
            return true;
        }
        return false;
    }
    case State::HALF_OPEN:
        return true;
    }
    return true;
}

void MprpcChannel::CircuitBreaker::onSuccess()
{
    m_failure_count = 0;
    if (m_state == State::HALF_OPEN)
    {
        LOG_INFO("Circuit breaker CLOSED (recovered)");
        m_state = State::CLOSED;
    }
}

void MprpcChannel::CircuitBreaker::onFailure()
{
    m_failure_count++;
    m_last_failure_time = std::chrono::steady_clock::now();
    if (m_failure_count >= m_threshold)
    {
        LOG_ERROR("Circuit breaker OPEN (threshold=%d)", m_threshold);
        m_state = State::OPEN;
    }
}

// ─── ZK Watcher ────────────────────────────────────────────────

void MprpcChannel::serviceWatcher(zhandle_t* zh, int type, int state,
                                   const char* path, void* watcherCtx)
{
    if (!watcherCtx) return;
    // Handle all ZK events that indicate service topology change
    if (type != ZOO_CREATED_EVENT && type != ZOO_DELETED_EVENT
        && type != ZOO_CHANGED_EVENT && type != ZOO_CHILD_EVENT) return;

    MprpcChannel* channel = static_cast<MprpcChannel*>(watcherCtx);
    LOG_INFO("ZK watcher triggered: type=%d, state=%d, path=%s", type, state, path);
    channel->invalidateCache(path);
}

void MprpcChannel::invalidateCache(const std::string& method_path)
{
    std::lock_guard<std::mutex> lock(m_cache_mtx);
    m_cache_timestamps.erase(method_path);
    m_watcher_registered[method_path] = false;
    LOG_INFO("Cache invalidated for %s (next call will re-query ZK)", method_path.c_str());
}

void MprpcChannel::registerWatcher(const std::string& method_path)
{
    {
        std::lock_guard<std::mutex> lock(m_cache_mtx);
        if (m_watcher_registered[method_path]) return;
    }

    zhandle_t* zh = m_zkclient->getHandle();
    if (!zh || !m_zkclient->isConnected()) return;

    // Register child watcher (watches child add/remove under method_path)
    struct String_vector str_vec;
    int rc = zoo_wget_children(zh, method_path.c_str(), serviceWatcher, this, &str_vec);
    if (rc == ZOK)
    {
        deallocate_String_vector(&str_vec);
        std::lock_guard<std::mutex> lock(m_cache_mtx);
        m_watcher_registered[method_path] = true;
        LOG_INFO("ZK child watcher registered for %s", method_path.c_str());
        return;
    }

    // Node doesn't exist — register existence watcher (watches creation)
    if (rc == ZNONODE)
    {
        struct Stat stat;
        rc = zoo_wexists(zh, method_path.c_str(), serviceWatcher, this, &stat);
        if (rc == ZOK)
        {
            std::lock_guard<std::mutex> lock(m_cache_mtx);
            m_watcher_registered[method_path] = true;
            LOG_INFO("ZK existence watcher registered for %s", method_path.c_str());
            return;
        }
    }

    LOG_WARN("Failed to register ZK watcher for %s, rc=%d",
             method_path.c_str(), rc);
}

// ─── Service Discovery ─────────────────────────────────────────

std::string MprpcChannel::discoverService(const std::string& method_path)
{
    // ── Direct address mode: bypass ZK entirely ──
    if (!m_direct_addr.empty())
    {
        m_last_provider = m_direct_addr;
        return m_direct_addr;
    }

    // ── Check cache first (avoids ZK query on every call) ──
    {
        std::lock_guard<std::mutex> lock(m_cache_mtx);
        auto ts_it = m_cache_timestamps.find(method_path);
        if (ts_it != m_cache_timestamps.end())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - ts_it->second).count();
            if (elapsed < CACHE_TTL_SEC)
            {
                auto addr_it = m_addr_cache.find(method_path);
                if (addr_it != m_addr_cache.end() && !addr_it->second.empty())
                {
                    m_last_provider = addr_it->second;
                    return addr_it->second;
                }
            }
        }
    }

    // ── Try ZK: list children for multi-provider support ──
    if (m_zk_available)
    {
        for (int i = 0; i < MAX_RETRY; ++i)
        {
            std::vector<std::string> children = m_zkclient->GetChildren(method_path.c_str());
            if (!children.empty())
            {
                // Pick a random child for basic load balancing
                std::string chosen = children[rand() % children.size()];
                std::string child_path = method_path + "/" + chosen;
                std::string addr = m_zkclient->GetData(child_path.c_str());
                if (!addr.empty())
                {
                    {
                        std::lock_guard<std::mutex> lock(m_cache_mtx);
                        m_addr_cache[method_path] = addr;
                        m_cache_timestamps[method_path] = std::chrono::steady_clock::now();
                    }

                    // Register ZK child watcher for real-time service awareness
                    registerWatcher(method_path);

                    m_last_provider = addr;
                    return addr;
                }
            }

            // Fallback: try direct GetData for backward compatibility with old single-writer registration
            std::string addr = m_zkclient->GetData(method_path.c_str());
            if (!addr.empty())
            {
                {
                    std::lock_guard<std::mutex> lock(m_cache_mtx);
                    m_addr_cache[method_path] = addr;
                    m_cache_timestamps[method_path] = std::chrono::steady_clock::now();
                }
                registerWatcher(method_path);
                m_last_provider = addr;
                return addr;
            }

            if (i < MAX_RETRY - 1)
            {
                int backoff = (1000 << i) + (rand() % 500);
                LOG_WARN("ZK discovery retry %d/%d in %dms for %s", i + 1, MAX_RETRY, backoff, method_path.c_str());
                usleep(backoff * 1000);
            }
        }
        m_zk_available = false;
    }

    // ── Try to register existence watcher (in case node disappeared) ──
    registerWatcher(method_path);

    // ── Degrade: use cached address ──
    {
        std::lock_guard<std::mutex> lock(m_cache_mtx);
        auto it = m_addr_cache.find(method_path);
        if (it != m_addr_cache.end())
        {
            LOG_WARN("ZK unavailable, using cached address for %s", method_path.c_str());
            m_last_provider = it->second;
            return it->second;
        }
    }

    return "";
}

// ─── Parse host_data and fill sockaddr ─────────────────────────

void MprpcChannel::parseAndConnect(const std::string& host_data,
                                    struct sockaddr_in& server_addr,
                                    std::string& ip, uint16_t& port)
{
    int idx = host_data.find(":");
    if (idx == -1) return;
    ip = host_data.substr(0, idx);
    port = atoi(host_data.substr(idx + 1).c_str());
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
}

// ─── Connect with Timeout ──────────────────────────────────────

bool MprpcChannel::connectWithTimeout(int clientfd, const struct sockaddr_in& addr, int timeout_sec)
{
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(clientfd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        LOG_ERROR("connect timeout (%ds), errno=%d", timeout_sec, errno);
        return false;
    }
    return true;
}

// ─── Constructor ───────────────────────────────────────────────

MprpcChannel::MprpcChannel() : m_zk_available(true)
{
    m_zkclient = MprpcApplication::GetZKClient();
    if (m_zkclient && m_zkclient->isConnected())
    {
        LOG_INFO("MprpcChannel: ZKClient acquired (shared)");

        m_zkclient->addReconnectCallback([this]() {
            LOG_INFO("ZK reconnected: clearing watcher state and cache");
            std::lock_guard<std::mutex> lock(m_cache_mtx);
            for (auto& kv : m_watcher_registered)
                kv.second = false;
            m_cache_timestamps.clear();
        });
    }
    else
    {
        m_zk_available = false;
        LOG_ERROR("MprpcChannel: ZKClient init failed, will use cached addresses");
    }

    MprpcConfig& config = MprpcApplication::GetConfig();
    std::string redis_ip = config.Load("redisip");
    std::string redis_port = config.Load("redisport");
    if (!redis_ip.empty() && !redis_port.empty())
    {
        auto redis = std::make_shared<RedisClient>();
        if (redis->Connect(redis_ip, atoi(redis_port.c_str()),
                           config.Load("redispassword")))
        {
            m_redis = redis;
            LOG_INFO("MprpcChannel: Redis connected at %s:%s", redis_ip.c_str(), redis_port.c_str());

            std::string enable_metrics = config.Load("metricsenable");
            if (enable_metrics.empty() || enable_metrics == "true")
            {
                int ttl = 3600;
                std::string ttl_str = config.Load("metricsttl");
                if (!ttl_str.empty()) ttl = std::stoi(ttl_str);
                m_metrics = std::make_unique<MetricsCollector>(m_redis, ttl);
                LOG_INFO("MprpcChannel: MetricsCollector initialized (ttl=%d)", ttl);
            }

            std::string enable_consumer = config.Load("ratelimitenableconsumer");
            if (enable_consumer == "true")
            {
                int max_qps = std::stoi(
                    config.Load("ratelimitmaxqps").empty() ? "1000"
                    : config.Load("ratelimitmaxqps"));
                int window_sec = std::stoi(
                    config.Load("ratelimitwindowsec").empty() ? "1"
                    : config.Load("ratelimitwindowsec"));
                int algo = std::stoi(
                    config.Load("ratelimitalgorithm").empty() ? "3"
                    : config.Load("ratelimitalgorithm"));

                m_client_rate_limiter = std::make_unique<RateLimiter>(
                    m_redis, max_qps, window_sec,
                    static_cast<RateLimitAlgorithm>(algo));
                LOG_INFO("MprpcChannel: Client rate limiter initialized");
            }
        }
        else
        {
            LOG_ERROR("MprpcChannel: Redis init failed, metrics disabled");
        }
    }
}

// ─── Destructor ────────────────────────────────────────────────

MprpcChannel::~MprpcChannel()
{
    for (auto& kv : m_persistent_fds)
    {
        if (kv.second >= 0)
            close(kv.second);
    }
    m_persistent_fds.clear();
}

// ─── CallMethod ────────────────────────────────────────────────

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* done)
{
    auto call_start = std::chrono::steady_clock::now();
    constexpr int64_t TOTAL_TIMEOUT_MS = 8000;

    const google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();
    std::string method_path = "/" + service_name + "/" + method_name;

    // ── Serialize request (done once, reused across retries) ──
    uint32_t args_size = 0;
    std::string args_str;
    if (!request->SerializeToString(&args_str))
    {
        auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::SERIALIZE_FAILED, 0, "MPRPC");
        static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, "serialize request error!"));
        return;
    }
    args_size = args_str.size();

    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);
    rpcHeader.set_version(1);  // 协议版本1

    uint32_t header_size = 0;
    std::string rpc_header_str;
    if (!rpcHeader.SerializeToString(&rpc_header_str))
    {
        auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::SERIALIZE_FAILED, 0, "MPRPC");
        static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, "serialize rpc header error!"));
        return;
    }
    header_size = rpc_header_str.size();

    std::string send_rpc_str;
    send_rpc_str.insert(0, std::string((char*)&header_size, 4));
    send_rpc_str += rpc_header_str;
    send_rpc_str += args_str;

    // ── Consumer-side rate limiting ──
    if (m_client_rate_limiter)
    {
        if (!m_client_rate_limiter->allow(service_name, method_name, ""))
        {
            std::string err = "client rate limit exceeded for " + method_path;
            LOG_ERROR("%s", err.c_str());
            static_cast<MprpcController*>(controller)->SetFailed(err);

            if (m_metrics)
                m_metrics->recordBlocked(service_name, method_name);
            return;
        }
    }

    // ── Retry loop ──
    std::string last_error;
    bool is_timeout = false;

    LOG_INFO("CallMethod: starting retry loop for %s", method_path.c_str());

    for (int attempt = 0; attempt < MAX_RETRY; ++attempt)
    {
        // Check overall timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - call_start).count();
        if (elapsed > TOTAL_TIMEOUT_MS)
        {
            last_error = "RPC call timeout (overall)";
            is_timeout = true;
            break;
        }

        // ── Check method-level circuit breaker ──
        {
            std::lock_guard<std::mutex> lock(m_cb_mtx);
            auto& cb = m_method_breakers[method_path];
            if (!cb.allowRequest())
            {
                last_error = "circuit breaker open for " + method_path;
                continue;
            }
        }

        // ── Discover service ──
        std::string host_data = discoverService(method_path);
        if (host_data.empty())
        {
            last_error = "service discovery failed for " + method_path;
            std::lock_guard<std::mutex> lock(m_cb_mtx);
            m_method_breakers[method_path].onFailure();
            continue;
        }

        // ── Parse host_data ──
        struct sockaddr_in server_addr;
        std::string ip;
        uint16_t port = 0;
        parseAndConnect(host_data, server_addr, ip, port);
        if (ip.empty() || port == 0)
        {
            last_error = method_path + " address invalid: " + host_data;
            continue;
        }

        // ── Try persistent TCP connection ──
        std::string conn_key = method_path + "@" + host_data;
        int clientfd = -1;
        bool need_new_conn = true;

        {
            auto it = m_persistent_fds.find(conn_key);
            if (it != m_persistent_fds.end())
            {
                clientfd = it->second;
                ssize_t sent = send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), MSG_NOSIGNAL);
                if (sent != -1)
                {
                    need_new_conn = false;
                }
                else
                {
                    close(clientfd);
                    m_persistent_fds.erase(it);
                }
            }
        }

        if (need_new_conn)
        {
            clientfd = socket(AF_INET, SOCK_STREAM, 0);
            if (clientfd == -1)
            {
                char errtxt[512];
                snprintf(errtxt, sizeof(errtxt), "create socket error! errno:%d", errno);
                auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::NETWORK_CONNECT_FAILED, errno, "MPRPC");
                static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, errtxt));
                return;
            }

            bool conn_ok = connectWithTimeout(clientfd, server_addr, 1);
            if (!conn_ok)
            {
                close(clientfd);
                char errtxt[512];
                snprintf(errtxt, sizeof(errtxt), "connect to %s:%d failed! errno:%d", ip.c_str(), port, errno);
                last_error = errtxt;
                {
                    std::lock_guard<std::mutex> lock(m_cb_mtx);
                    m_method_breakers[method_path].onFailure();
                }
                continue;
            }

            m_persistent_fds[conn_key] = clientfd;

            if (send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), MSG_NOSIGNAL) == -1)
            {
                close(clientfd);
                m_persistent_fds.erase(conn_key);
                char errtxt[512];
                snprintf(errtxt, sizeof(errtxt), "send error! errno:%d", errno);
                auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_SEND_FAILED, errno, "MPRPC");
                static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, errtxt));
                {
                    std::lock_guard<std::mutex> lock(m_cb_mtx);
                    m_method_breakers[method_path].onFailure();
                }
                continue;
            }
        }

        // ── Receive response with timeout ──
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Step 1: Read 4-byte header length
        uint32_t response_header_size = 0;
        {
            char len_buf[4] = {0};
            int total_read = 0;
            while (total_read < 4)
            {
                int n = recv(clientfd, len_buf + total_read, 4 - total_read, 0);
                if (n <= 0)
                {
                    close(clientfd);
                    m_persistent_fds.erase(conn_key);
                    if (n == 0) last_error = "connection closed by server";
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) { last_error = "RPC recv header timeout (5s)"; is_timeout = true; }
                    else { char errtxt[512]; snprintf(errtxt, sizeof(errtxt), "recv header error! errno:%d", errno);
                           auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_RECV_FAILED, errno, "MPRPC");
                           static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, errtxt)); }
                    {
                        std::lock_guard<std::mutex> lock(m_cb_mtx);
                        m_method_breakers[method_path].onFailure();
                    }
                    continue;
                }
                total_read += n;
            }
            memcpy(&response_header_size, len_buf, 4);
        }

        // Step 2: Validate header size
        if (response_header_size > 1024 * 1024)
        {
            close(clientfd);
            m_persistent_fds.erase(conn_key);
            last_error = "invalid response header size: too large";
            continue;
        }

        // Step 3: Read header bytes
        std::string response_rpc_header_str(response_header_size, '\0');
        {
            int total_read = 0;
            while (total_read < static_cast<int>(response_header_size))
            {
                int n = recv(clientfd, &response_rpc_header_str[total_read], 
                            response_header_size - total_read, 0);
                if (n <= 0)
                {
                    close(clientfd);
                    m_persistent_fds.erase(conn_key);
                    if (n == 0) last_error = "connection closed by server";
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) { last_error = "RPC recv header body timeout"; is_timeout = true; }
                    else { char errtxt[512]; snprintf(errtxt, sizeof(errtxt), "recv header body error! errno:%d", errno);
                           auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_RECV_FAILED, errno, "MPRPC");
                           static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, errtxt)); }
                    {
                        std::lock_guard<std::mutex> lock(m_cb_mtx);
                        m_method_breakers[method_path].onFailure();
                    }
                    continue;
                }
                total_read += n;
            }
        }

        // Step 4: Parse header
        mprpc::RpcHeader response_rpcHeader;
        if (!response_rpcHeader.ParseFromString(response_rpc_header_str))
        {
            close(clientfd);
            m_persistent_fds.erase(conn_key);
            last_error = "parse response header error";
            continue;
        }

        uint32_t response_args_size = response_rpcHeader.args_size();
        if (response_args_size == 0)
        {
            close(clientfd);
            m_persistent_fds.erase(conn_key);
            last_error = "server error: empty args";
            continue;
        }

        // Step 5: Read args bytes (dynamic allocation)
        std::string response_str(response_args_size, '\0');
        {
            int total_read = 0;
            while (total_read < static_cast<int>(response_args_size))
            {
                int n = recv(clientfd, &response_str[total_read], 
                            response_args_size - total_read, 0);
                if (n <= 0)
                {
                    close(clientfd);
                    m_persistent_fds.erase(conn_key);
                    if (n == 0) last_error = "connection closed by server";
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) { last_error = "RPC recv args timeout"; is_timeout = true; }
                    else { char errtxt[512]; snprintf(errtxt, sizeof(errtxt), "recv args error! errno:%d", errno);
                           auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_RECV_FAILED, errno, "MPRPC");
                           static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, errtxt)); }
                    {
                        std::lock_guard<std::mutex> lock(m_cb_mtx);
                        m_method_breakers[method_path].onFailure();
                    }
                    continue;
                }
                total_read += n;
            }
        }

        // Step 6: Parse response
        if (!response->ParseFromString(response_str))
        {
            close(clientfd);
            m_persistent_fds.erase(conn_key);
            auto ec = RpcErrorUtil::createFrameError(FrameErrorCode::DESERIALIZE_FAILED, 0, "MPRPC");
            static_cast<MprpcController*>(controller)->SetFailed(RpcError(ec, "parse response error"));
            return;
        }

        // ── Success (keep connection alive, do NOT close) ──
        {
            std::lock_guard<std::mutex> lock(m_cb_mtx);
            m_method_breakers[method_path].onSuccess();
        }
        m_zk_available = true;

        if (m_metrics)
        {
            auto end = std::chrono::steady_clock::now();
            int64_t latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - call_start).count();
            m_metrics->recordCall(service_name, method_name, latency, false, false);
        }
        return;
    }

    // All retries exhausted
    if (!last_error.empty())
    {
        static_cast<MprpcController*>(controller)->SetFailed(last_error);

        if (m_metrics)
        {
            auto end = std::chrono::steady_clock::now();
            int64_t latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - call_start).count();
            m_metrics->recordCall(service_name, method_name, latency, true, is_timeout);
        }
    }
}
