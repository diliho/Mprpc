# Redis 中间件集成

> 本文档系统性地阐述 Redis 四大核心能力（限流、分布式锁、高可用、淘汰策略）的原理与实践，并详细说明如何将 Redis 集成到 MPRPC 框架中，使项目更完整、更强壮。

---

## 目录

1. [为什么需要 Redis](#1-为什么需要-redis)
2. [Redis 客户端封装与连接池](#2-redis-客户端封装与连接池)
3. [分布式限流（Rate Limiting）](#3-分布式限流rate-limiting)
4. [分布式锁（Distributed Lock）](#4-分布式锁distributed-lock)
5. [高可用方案（High Availability）](#5-高可用方案high-availability)
6. [淘汰策略（Eviction Policy）](#6-淘汰策略eviction-policy)
7. [实时监控指标](#7-实时监控指标)
8. [项目集成指南](#8-项目集成指南)
9. [数据流总图](#9-数据流总图)
10. [收益总结](#10-收益总结)
11. [注意事项](#11-注意事项)

---

## 1. 为什么需要 Redis

### 1.1 当前项目的关键空白

| 当前问题 | 后果 | Redis 解决方案 |
|---------|------|---------------|
| 服务发现缓存仅存于内存，Consumer 重启即丢失 | ZK 故障期间重启 Consumer → 完全无法发现服务 | Redis 作为共享缓存层，跨进程持久化实例列表 |
| 熔断器状态仅存于内存，Consumer 重启后重置为 CLOSE | 已熔断的故障服务在重启后会再次被调用 5 次（阈值），增加恢复压力 | Redis 持久化熔断器状态，重启后立即恢复 OPEN |
| 无分布式限流 | 单个 Consumer 可无限量请求 Provider，无法做全局流量控制 | Redis INCR + EXPIRE / ZSET 滑动窗口实现跨进程限流 |
| N 个 Consumer 各自独立查询 ZK | ZK 读压力随 Consumer 数量线性增长 | Redis 作为缓存层，1000 个 Consumer 共享同一份缓存 |
| 无分布式锁 | 多个 Consumer 同时操作共享资源时可能出现竞态条件 | Redis SETNX / Redlock 实现分布式锁 |
| 无实时监控指标 | 仅靠日志文件排查问题，效率低 | Redis 计数器 / 有序集合存储实时调用量、错误率、延迟 |

### 1.2 Redis 在整个架构中的定位

```
                   ┌──────────────────────┐
                   │     Zookeeper         │  ← 服务注册与协调（写路径）
                   │  (临时节点 / Watcher) │
                   └──────┬───────────────┘
                          │ ① 注册/发现
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
     ┌──────────┐  ┌──────────┐  ┌──────────┐
     │Consumer 1│  │Consumer 2│  │Consumer N│
     └─────┬────┘  └─────┬────┘  └────┬─────┘
           │              │             │
           ▼              ▼             ▼
     ┌─────────────────────────────────────────┐
     │                  Redis                    │  ← 读加速 / 辅助能力
     │  + 服务发现缓存 (降低 ZK 读压力)          │
     │  + 熔断器状态持久化 (重启不丢失)          │
     │  + 分布式限流 (全局 QPS 控制)             │
     │  + 分布式锁 (临界资源互斥)                │
     │  + 实时监控 (QPS/延迟/错误率)             │
     └─────────────────────────────────────────┘
```

---

## 2. Redis 客户端封装与连接池

### 2.1 hiredis 简介

[hiredis](https://github.com/redis/hiredis) 是 Redis 官方推荐的 C 语言客户端库，提供了同步 API 和异步 API。本项目基于 hiredis 封装 `RedisClient`。

### 2.2 RedisClient 类设计

```cpp
// src/include/redisutil.h
#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <memory>
#include <atomic>

class RedisClient {
public:
    RedisClient();
    ~RedisClient();

    // ── 连接管理 ──
    bool Connect(const std::string& ip, int port,
                 const std::string& password = "",
                 int timeout_sec = 3);
    bool IsConnected() const;
    void Disconnect();
    bool Reconnect();

    // ── 自动重连回调 ──
    void setReconnectCallback(std::function<void()> cb) { m_reconnect_cb = std::move(cb); }

    // ── Key 操作 ──
    bool Exists(const std::string& key);
    bool Expire(const std::string& key, int seconds);
    int64_t TTL(const std::string& key);
    bool Del(const std::string& key);

    // ── String 操作 ──
    std::string Get(const std::string& key);
    bool Set(const std::string& key, const std::string& value);
    bool SetEx(const std::string& key, const std::string& value, int ttl_sec);
    bool SetNx(const std::string& key, const std::string& value);
    int64_t Incr(const std::string& key);
    int64_t IncrBy(const std::string& key, int64_t delta);
    int64_t Decr(const std::string& key);

    // ── Hash 操作 ──
    bool HSet(const std::string& key, const std::string& field, const std::string& value);
    std::string HGet(const std::string& key, const std::string& field);
    bool HDel(const std::string& key, const std::string& field);
    int64_t HLen(const std::string& key);
    std::unordered_map<std::string, std::string> HGetAll(const std::string& key);
    std::vector<std::string> HKeys(const std::string& key);
    std::vector<std::string> HVals(const std::string& key);

    // ── List 操作 ──
    int64_t LPush(const std::string& key, const std::string& value);
    int64_t RPush(const std::string& key, const std::string& value);
    std::string LPop(const std::string& key);
    std::string RPop(const std::string& key);
    int64_t LLen(const std::string& key);
    std::vector<std::string> LRange(const std::string& key, int start, int stop);

    // ── Set 操作 ──
    int64_t SAdd(const std::string& key, const std::string& member);
    int64_t SRem(const std::string& key, const std::string& member);
    bool SIsMember(const std::string& key, const std::string& member);
    int64_t SCard(const std::string& key);
    std::vector<std::string> SMembers(const std::string& key);

    // ── ZSet 操作 ──
    bool ZAdd(const std::string& key, double score, const std::string& member);
    int64_t ZRem(const std::string& key, const std::string& member);
    int64_t ZCard(const std::string& key);
    int64_t ZCount(const std::string& key, double min, double max);
    bool ZRemRangeByScore(const std::string& key, double min, double max);
    bool ZRemRangeByRank(const std::string& key, int start, int stop);
    std::vector<std::string> ZRange(const std::string& key, int start, int stop, bool with_scores = false);
    std::vector<std::string> ZRevRange(const std::string& key, int start, int stop, bool with_scores = false);
    int64_t ZRank(const std::string& key, const std::string& member);
    double ZScore(const std::string& key, const std::string& member);

    // ── 分布式锁 ──
    bool Lock(const std::string& key, const std::string& value, int ttl_sec);
    bool Unlock(const std::string& key, const std::string& value);

    // ── Lua 脚本 ──
    std::string Eval(const std::string& script,
                     const std::vector<std::string>& keys,
                     const std::vector<std::string>& args);

private:
    redisContext* m_context;
    std::string m_ip;
    int m_port;
    std::string m_password;
    int m_timeout_sec;
    std::function<void()> m_reconnect_cb;
    mutable std::mutex m_mtx;

    bool auth();
    bool ensureConnected();
    redisReply* executeCommand(const char* format, ...);
    void freeReply(redisReply* reply);
};
```

### 2.3 RedisClient 核心实现

```cpp
// src/redisutil.cc
#include "redisutil.h"
#include "logger.h"
#include <cstring>
#include <cstdarg>

RedisClient::RedisClient() : m_context(nullptr), m_port(6379), m_timeout_sec(3) {}

RedisClient::~RedisClient()
{
    Disconnect();
}

bool RedisClient::Connect(const std::string& ip, int port,
                          const std::string& password, int timeout_sec)
{
    std::lock_guard<std::mutex> lock(m_mtx);
    m_ip = ip;
    m_port = port;
    m_password = password;
    m_timeout_sec = timeout_sec;

    struct timeval tv = {timeout_sec, 0};
    m_context = redisConnectWithTimeout(ip.c_str(), port, tv);
    if (!m_context || m_context->err)
    {
        LOG_ERROR("Redis connect failed: %s",
                  m_context ? m_context->errstr : "allocation failed");
        if (m_context)
        {
            redisFree(m_context);
            m_context = nullptr;
        }
        return false;
    }

    if (!password.empty() && !auth())
    {
        redisFree(m_context);
        m_context = nullptr;
        return false;
    }

    LOG_INFO("Redis connected to %s:%d", ip.c_str(), port);
    return true;
}

bool RedisClient::auth()
{
    redisReply* reply = (redisReply*)redisCommand(m_context, "AUTH %s", m_password.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        LOG_ERROR("Redis AUTH failed");
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

bool RedisClient::IsConnected() const
{
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_context && !m_context->err;
}

void RedisClient::Disconnect()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_context)
    {
        redisFree(m_context);
        m_context = nullptr;
    }
}

bool RedisClient::Reconnect()
{
    Disconnect();
    bool ok = Connect(m_ip, m_port, m_password, m_timeout_sec);
    if (ok && m_reconnect_cb)
    {
        m_reconnect_cb();
    }
    return ok;
}

bool RedisClient::ensureConnected()
{
    if (m_context && !m_context->err) return true;
    LOG_WARN("Redis connection lost, reconnecting...");
    return Reconnect();
}

redisReply* RedisClient::executeCommand(const char* format, ...)
{
    if (!ensureConnected()) return nullptr;

    std::lock_guard<std::mutex> lock(m_mtx);
    va_list ap;
    va_start(ap, format);
    redisReply* reply = (redisReply*)redisvCommand(m_context, format, ap);
    va_end(ap);

    if (!reply)
    {
        LOG_ERROR("Redis command failed: connection error");
        // Mark for reconnection on next call
        return nullptr;
    }
    return reply;
}

void RedisClient::freeReply(redisReply* reply)
{
    if (reply) freeReplyObject(reply);
}

// ── String 操作 ──

std::string RedisClient::Get(const std::string& key)
{
    redisReply* reply = executeCommand("GET %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        if (reply) freeReply(reply);
        return "";
    }
    std::string val = reply->str;
    freeReply(reply);
    return val;
}

bool RedisClient::Set(const std::string& key, const std::string& value)
{
    redisReply* reply = executeCommand("SET %s %s", key.c_str(), value.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

bool RedisClient::SetEx(const std::string& key, const std::string& value, int ttl_sec)
{
    redisReply* reply = executeCommand("SETEX %s %d %s", key.c_str(), ttl_sec, value.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

bool RedisClient::SetNx(const std::string& key, const std::string& value)
{
    redisReply* reply = executeCommand("SETNX %s %s", key.c_str(), value.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    int64_t result = reply->integer;
    freeReply(reply);
    return result == 1;
}

int64_t RedisClient::Incr(const std::string& key)
{
    redisReply* reply = executeCommand("INCR %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        if (reply) freeReply(reply);
        return -1;
    }
    int64_t val = reply->integer;
    freeReply(reply);
    return val;
}

// ── Hash 操作 ──

bool RedisClient::HSet(const std::string& key, const std::string& field, const std::string& value)
{
    redisReply* reply = executeCommand("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

std::string RedisClient::HGet(const std::string& key, const std::string& field)
{
    redisReply* reply = executeCommand("HGET %s %s", key.c_str(), field.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        if (reply) freeReply(reply);
        return "";
    }
    std::string val = reply->str;
    freeReply(reply);
    return val;
}

std::unordered_map<std::string, std::string> RedisClient::HGetAll(const std::string& key)
{
    std::unordered_map<std::string, std::string> result;
    redisReply* reply = executeCommand("HGETALL %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY)
    {
        if (reply) freeReply(reply);
        return result;
    }
    for (size_t i = 0; i < reply->elements; i += 2)
    {
        std::string field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        result[field] = value;
    }
    freeReply(reply);
    return result;
}

// ── ZSet 操作 ──

bool RedisClient::ZAdd(const std::string& key, double score, const std::string& member)
{
    redisReply* reply = executeCommand("ZADD %s %f %s", key.c_str(), score, member.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

int64_t RedisClient::ZCard(const std::string& key)
{
    redisReply* reply = executeCommand("ZCARD %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        if (reply) freeReply(reply);
        return -1;
    }
    int64_t val = reply->integer;
    freeReply(reply);
    return val;
}

bool RedisClient::ZRemRangeByScore(const std::string& key, double min, double max)
{
    redisReply* reply = executeCommand("ZREMRANGEBYSCORE %s %f %f", key.c_str(), min, max);
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}

// ── 分布式锁 ──

bool RedisClient::Lock(const std::string& key, const std::string& value, int ttl_sec)
{
    // SET key value NX EX ttl_sec — 原子性加锁
    redisReply* reply = executeCommand("SET %s %s NX EX %d",
                                       key.c_str(), value.c_str(), ttl_sec);
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_STATUS
               && strcasecmp(reply->str, "OK") == 0);
    freeReply(reply);
    return ok;
}

bool RedisClient::Unlock(const std::string& key, const std::string& value)
{
    // Lua 脚本保证原子性：先校验 value 再删除
    const char* script =
        "if redis.call('get', KEYS[1]) == ARGV[1] then "
        "    return redis.call('del', KEYS[1]) "
        "else "
        "    return 0 "
        "end";
    redisReply* reply = (redisReply*)redisCommand(
        m_context, "EVAL %s 1 %s %s", script, key.c_str(), value.c_str());
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReply(reply);
    return ok;
}

// ── Lua 脚本 ──

std::string RedisClient::Eval(const std::string& script,
                              const std::vector<std::string>& keys,
                              const std::vector<std::string>& args)
{
    // Build EVAL command with dynamic args
    // For simplicity, use redisCommand with formatting
    // In production, consider a more robust approach
    std::string cmd = "EVAL '" + script + "' " + std::to_string(keys.size());
    for (const auto& k : keys) cmd += " '" + k + "'";
    for (const auto& a : args) cmd += " '" + a + "'";

    redisReply* reply = executeCommand(cmd.c_str());
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return "";
    }
    std::string result = reply->str ? reply->str : "";
    freeReply(reply);
    return result;
}

// ── 通用操作 ──

bool RedisClient::Exists(const std::string& key)
{
    redisReply* reply = executeCommand("EXISTS %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        if (reply) freeReply(reply);
        return false;
    }
    bool exists = (reply->integer == 1);
    freeReply(reply);
    return exists;
}

bool RedisClient::Expire(const std::string& key, int seconds)
{
    redisReply* reply = executeCommand("EXPIRE %s %d", key.c_str(), seconds);
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply) freeReply(reply);
        return false;
    }
    freeReply(reply);
    return true;
}
```

### 2.4 连接池 RedisConnectionPool

hiredis 的 `redisContext` 非线程安全，因此多线程环境下每个线程应当使用独立连接，或通过连接池管理。

```cpp
// src/include/redispool.h
#pragma once
#include "redisutil.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <atomic>

class RedisConnectionPool {
public:
    static RedisConnectionPool& GetInstance();

    bool Init(const std::string& ip, int port,
              const std::string& password = "",
              int pool_size = 8,
              int timeout_sec = 3);

    std::shared_ptr<RedisClient> GetConnection();
    void ReturnConnection(std::shared_ptr<RedisClient> conn);
    void Close();

private:
    RedisConnectionPool() = default;
    ~RedisConnectionPool();
    RedisConnectionPool(const RedisConnectionPool&) = delete;
    RedisConnectionPool& operator=(const RedisConnectionPool&) = delete;

    std::queue<std::shared_ptr<RedisClient>> m_pool;
    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::string m_ip;
    int m_port;
    std::string m_password;
    int m_pool_size;
    int m_timeout_sec;
    std::atomic<bool> m_initialized{false};
    std::atomic<int> m_active_count{0};

    std::shared_ptr<RedisClient> createConnection();
};
```

```cpp
// src/redispool.cc
#include "redispool.h"
#include "logger.h"

RedisConnectionPool& RedisConnectionPool::GetInstance()
{
    static RedisConnectionPool pool;
    return pool;
}

bool RedisConnectionPool::Init(const std::string& ip, int port,
                               const std::string& password,
                               int pool_size, int timeout_sec)
{
    m_ip = ip;
    m_port = port;
    m_password = password;
    m_pool_size = pool_size;
    m_timeout_sec = timeout_sec;

    for (int i = 0; i < pool_size; ++i)
    {
        auto conn = createConnection();
        if (!conn)
        {
            LOG_ERROR("RedisConnectionPool: failed to create connection %d/%d",
                      i + 1, pool_size);
            return false;
        }
        m_pool.push(conn);
    }

    m_initialized = true;
    LOG_INFO("RedisConnectionPool initialized with %d connections", pool_size);
    return true;
}

std::shared_ptr<RedisClient> RedisConnectionPool::createConnection()
{
    auto conn = std::make_shared<RedisClient>();
    if (!conn->Connect(m_ip, m_port, m_password, m_timeout_sec))
    {
        return nullptr;
    }
    return conn;
}

std::shared_ptr<RedisClient> RedisConnectionPool::GetConnection()
{
    std::unique_lock<std::mutex> lock(m_mtx);
    if (m_pool.empty())
    {
        // Try to create a new connection (if below max)
        if (m_active_count < m_pool_size * 2)
        {
            lock.unlock();
            auto conn = createConnection();
            if (conn)
            {
                m_active_count++;
                return conn;
            }
            lock.lock();
        }

        // Wait for a connection to be returned
        if (!m_cv.wait_for(lock, std::chrono::seconds(5),
                           [this]() { return !m_pool.empty(); }))
        {
            LOG_ERROR("RedisConnectionPool: get connection timeout");
            return nullptr;
        }
    }

    auto conn = m_pool.front();
    m_pool.pop();
    m_active_count++;
    return conn;
}

void RedisConnectionPool::ReturnConnection(std::shared_ptr<RedisClient> conn)
{
    if (!conn) return;
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!conn->IsConnected())
    {
        // Discard broken connection, create a new one
        conn = createConnection();
        if (!conn) return;
    }
    m_pool.push(conn);
    m_active_count--;
    m_cv.notify_one();
}

void RedisConnectionPool::Close()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    while (!m_pool.empty())
    {
        m_pool.front()->Disconnect();
        m_pool.pop();
    }
    m_initialized = false;
}

RedisConnectionPool::~RedisConnectionPool()
{
    Close();
}
```

> **🔑 连接池设计要点**：
> - 池大小通过配置 `redisconnectionpoolsize` 控制，默认 8
> - `GetConnection()` 在池为空时会等待最多 5s，避免突发流量打穿 Redis
> - 返回连接时自动检测连接健康状态，断连的连接会被自动替换
> - `m_active_count` 控制最大活跃连接数不超过 `pool_size * 2`

---

## 3. 分布式限流（Rate Limiting）

### 3.1 为什么需要分布式限流

在 RPC 框架中，限流的目的在于：

1. **保护 Provider**：防止单个 Consumer 的突发流量打垮 Provider
2. **公平调度**：在多 Consumer 之间公平分配服务资源
3. **服务等级保障**：为高优先级服务预留容量
4. **防止级联故障**：当一个服务过载时，限流可以防止故障传播

### 3.2 限流算法原理与对比

#### 3.2.1 固定窗口计数器（Fixed Window Counter）

**原理**：
将时间划分为固定窗口（如 1s），每个窗口内维护一个计数器，达到阈值后拒绝请求。窗口结束后计数器重置。

```
时间轴:  |---- 1s ----|---- 1s ----|---- 1s ----|
计数:    5 (阈值=10)   10 (拒绝)    3
```

**Redis 实现**：
```
Key:   mprpc:ratelimit:{method}:fixed:{window_start}
Value: INCR 计数
TTL:   窗口大小 + 1s
```

**优点**：实现极其简单，内存开销小
**缺点**：**窗口边界流量突发**——在窗口末尾和下一个窗口开头的交界处，可能出现 2 倍阈值的请求通过

```
窗口1 (计数 10/10) | 窗口2 (计数 10/10)
                   ^
           流量在这里突发: 20个请求几乎同时通过
```

#### 3.2.2 滑动窗口日志（Sliding Window Log）

**原理**：记录每个请求的时间戳，通过统计窗口内的时间戳数量来判断是否限流。

**Redis 实现（ZSET）**：
```
Key:      mprpc:ratelimit:{method}:{client_ip}
Member:   {request_id}  (唯一标识)
Score:    {timestamp_ms}
TTL:      窗口大小 + 1s
操作:     ZREMRANGEBYSCORE 清理过期 + ZCARD 统计
```

**优点**：边界精准，无突发问题
**缺点**：每个请求都需要记录，内存开销大；大窗口下 ZREMRANGEBYSCORE 可能耗时

#### 3.2.3 滑动窗口计数器（Sliding Window Counter）

**原理**：固定窗口 + 滑动加权。将窗口进一步分为更小的子窗口（如 1s 窗口分为 10 个 100ms 子窗口），通过计算当前子窗口和上一个子窗口的加权和得到计数。

```
窗口 = 1s, 子窗口 = 200ms
                    ↑ 当前时间
| 200 | 200 | 200 | 200 | 200 |
|  3  |  5  |  2  |  4  |  1  |
      ←──── 1s 窗口 ────→
      权重 = (1s - 已过时间) / 子窗口大小
      预估值 = 4 * 0.6 + 1 = 3.4
```

**Redis 实现**：
```
Key:   mprpc:ratelimit:{method}:sliding:{sub_window_idx}
Value: INCR 计数
TTL:   窗口大小 * 2
```

**优点**：内存开销小，边界平滑
**缺点**：精度取决于子窗口大小，是近似值

#### 3.2.4 令牌桶（Token Bucket）

**原理**：以固定速率向桶中添加令牌，每个请求消耗一个令牌。桶有容量上限。当桶为空时拒绝请求。

```
          ┌──────────────────────┐
          │    令牌桶 (容量 10)    │
          │  ⚪⚪⚪⚪⚪⚪⚪⚪⚪⚪   │  ← 每秒补充 rate 个
          └──────────────────────┘
                    │
              ┌─────▼──────┐
              │  请求消耗令牌  │
              └─────┬──────┘
                    ▼
              允许 / 拒绝
```

**Redis 实现**：
```
Key:      mprpc:ratelimit:{method}:token_bucket
Field:    tokens      → 当前令牌数 (float)
Field:    last_refill → 上次补充时间戳 (ms)
Type:     HASH
```

Lua 脚本实现：

```lua
-- token_bucket.lua
local key = KEYS[1]
local capacity = tonumber(ARGV[1])    -- 桶容量
local rate = tonumber(ARGV[2])        -- 每秒补充速率
local now = tonumber(ARGV[3])         -- 当前时间戳(ms)
local cost = tonumber(ARGV[4])        -- 本次消耗令牌数

local bucket = redis.call('HMGET', key, 'tokens', 'last_refill')
local tokens = tonumber(bucket[1] or capacity)
local last_refill = tonumber(bucket[2] or now)

-- 计算补充的令牌数
local elapsed = math.max(0, now - last_refill)
local new_tokens = math.min(capacity, tokens + elapsed * rate / 1000)

if new_tokens >= cost then
    new_tokens = new_tokens - cost
    redis.call('HMSET', key, 'tokens', new_tokens, 'last_refill', now)
    redis.call('EXPIRE', key, 86400)
    return 1  -- 允许
else
    redis.call('HMSET', key, 'tokens', new_tokens, 'last_refill', now)
    return 0  -- 拒绝
end
```

**优点**：允许一定的突发流量（桶满时可以短时间密集发送），同时长期平均速率可控
**缺点**：实现相对复杂，需要 Lua 脚本保证原子性

#### 3.2.5 漏桶（Leaky Bucket）

**原理**：请求以任意速率进入桶中，以固定速率从桶中漏出。桶有容量上限，溢出的请求被拒绝。

```
         入桶速率 (任意)
              │
              ▼
    ┌──────────────────┐
    │   漏桶 (容量 10)   │  ← 队列
    │ ████████████████  │
    └──────────────────┘
              │
              ▼
         出桶速率 (固定)
```

**Redis 实现**：使用 List 作为队列，LPUSH 入队，定期 BRPOP 消费。

```lua
-- leaky_bucket.lua
local key = KEYS[1]
local capacity = tonumber(ARGV[1])    -- 桶容量
local now = tonumber(ARGV[2])         -- 当前时间戳(ms)

local current = redis.call('LLEN', key)
if current >= capacity then
    return 0  -- 桶满，拒绝
end
redis.call('RPUSH', key, now)
redis.call('EXPIRE', key, 3600)
return 1  -- 允许
```

**优点**：出口速率绝对平滑，适合保护数据库等对流量平稳性要求高的场景
**缺点**：不能应对突发流量；队列满时直接丢弃请求

### 3.3 选择建议

| 算法 | 推荐场景 | 精确度 | 内存开销 | 实现复杂度 |
|------|---------|--------|---------|-----------|
| 固定窗口 | 简单限流，可接受边界突发 | 低 | 极低 | 极简 |
| 滑动窗口日志 | 需要精确统计的场景 | 高 | 高 | 中 |
| 滑动窗口计数器 | 精度与效率的平衡 | 中 | 低 | 中 |
| 令牌桶 | 允许突发、控制均速的场景 | 中 | 低 | 较高 |
| 漏桶 | 需要绝对平滑流量的场景 | 中 | 中 | 中 |

**本项目推荐**：Provider 侧用**令牌桶**（允许短时突发但不超均速），Consumer 侧用**滑动窗口计数器**（保护自己不超配额）。

### 3.4 RateLimiter 类设计

```cpp
// src/include/ratelimiter.h
#pragma once
#include <string>
#include <memory>
#include "redisutil.h"

// 限流算法枚举
enum class RateLimitAlgorithm {
    FIXED_WINDOW,       // 固定窗口
    SLIDING_WINDOW_LOG, // 滑动窗口日志 (ZSET)
    SLIDING_WINDOW_COUNTER, // 滑动窗口计数器
    TOKEN_BUCKET,       // 令牌桶
    LEAKY_BUCKET        // 漏桶
};

class RateLimiter {
public:
    // max_requests: 窗口内最大请求数
    // window_sec:   窗口大小（秒）
    // algorithm:    限流算法
    RateLimiter(std::shared_ptr<RedisClient> redis,
                int max_requests,
                int window_sec,
                RateLimitAlgorithm algorithm = RateLimitAlgorithm::SLIDING_WINDOW_LOG);

    ~RateLimiter() = default;

    // 判断请求是否允许通过
    // service_name / method_name: RPC 服务和方法
    // client_ip: 调用方 IP（可选，为空则不按 IP 隔离）
    bool allow(const std::string& service_name,
               const std::string& method_name,
               const std::string& client_ip = "");

    // 获取当前窗口内的请求计数
    int64_t getCurrentCount(const std::string& service_name,
                            const std::string& method_name,
                            const std::string& client_ip = "");

    // 动态调整限流参数
    void setMaxRequests(int max_requests);
    void setWindowSec(int window_sec);

    // 限流 Key 前缀
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

    // 各算法的具体实现
    bool allowFixedWindow(const std::string& key);
    bool allowSlidingWindowLog(const std::string& key);
    bool allowSlidingWindowCounter(const std::string& key);
    bool allowTokenBucket(const std::string& key);
    bool allowLeakyBucket(const std::string& key);
};
```

### 3.5 RateLimiter 实现

```cpp
// src/ratelimiter.cc
#include "ratelimiter.h"
#include "logger.h"
#include <sstream>

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
        LOG_WARN("RateLimiter: Redis not connected, allowing request by default");
        return true;  // 降级：Redis 不可用时放行
    }

    switch (m_algorithm)
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

// ── 固定窗口 ──
bool RateLimiter::allowFixedWindow(const std::string& key)
{
    int64_t count = m_redis->Incr(key);
    if (count == 1)
    {
        // 第一个请求，设置过期时间
        m_redis->Expire(key, m_window_sec);
    }
    return count <= m_max_requests;
}

// ── 滑动窗口日志（ZSET 实现，精确） ──
bool RateLimiter::allowSlidingWindowLog(const std::string& key)
{
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t window_start = now - m_window_sec * 1000;

    // Lua 脚本保证原子性
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
        std::to_string(m_max_requests),
        std::to_string(now),
        std::to_string(now) + ":" + std::to_string(rand()),
        std::to_string(m_window_sec + 1)
    };

    std::string result = m_redis->Eval(script, keys, args);
    return result == "1";
}

// ── 滑动窗口计数器 ──
bool RateLimiter::allowSlidingWindowCounter(const std::string& key)
{
    // 将窗口分为 10 个子窗口
    int sub_window_ms = (m_window_sec * 1000) / 10;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    int64_t current_sub = now / sub_window_ms;
    std::string current_key = key + ":sub:" + std::to_string(current_sub);

    // 当前子窗口计数 +1
    int64_t current_count = m_redis->Incr(current_key);
    if (current_count == 1)
    {
        // 设置过期时间为 2 倍窗口
        m_redis->Expire(current_key, m_window_sec * 2);
    }

    // 计算所有活跃子窗口的总和
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

    return total <= m_max_requests;
}

// ── 令牌桶（Lua 脚本实现） ──
bool RateLimiter::allowTokenBucket(const std::string& key)
{
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 桶容量 = max_requests (允许一定突发)
    // 补充速率 = max_requests / window_sec (维持窗口内平均速率)
    int capacity = m_max_requests;
    double rate = static_cast<double>(m_max_requests) / m_window_sec;

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

// ── 漏桶（List 实现） ──
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
        std::to_string(m_max_requests),
        std::to_string(now_ms)
    };

    std::string result = m_redis->Eval(script, keys, args);
    return result == "1";
}

void RateLimiter::setMaxRequests(int max_requests)
{
    m_max_requests = max_requests;
    LOG_INFO("RateLimiter max_requests updated to %d", max_requests);
}

void RateLimiter::setWindowSec(int window_sec)
{
    m_window_sec = window_sec;
    LOG_INFO("RateLimiter window_sec updated to %d", window_sec);
}
```

### 3.6 限流在 Provider 侧的集成

在 `mprpcprovider.cc` 的 `OnMessage()` 中，解析 RPC 请求后先进行限流检查：

```cpp
// 在 RpcProvider 类中增加成员
// mprpcprovider.h 新增:
// #include "ratelimiter.h"
// std::unique_ptr<RateLimiter> m_rate_limiter;

// RpcProvider::Run() 中初始化:
void RpcProvider::Run()
{
    // ... 原有代码 ...

    // 初始化限流器（从配置读取）
    std::string redis_ip = MprpcApplication::GetConfig().Load("redisip");
    std::string redis_port = MprpcApplication::GetConfig().Load("redisport");
    if (!redis_ip.empty() && !redis_port.empty())
    {
        auto redis = std::make_shared<RedisClient>();
        if (redis->Connect(redis_ip, atoi(redis_port.c_str()),
                           MprpcApplication::GetConfig().Load("redispassword")))
        {
            int max_qps = std::stoi(
                MprpcApplication::GetConfig().Load("ratelimit_maxqps").empty()
                ? "1000" : MprpcApplication::GetConfig().Load("ratelimit_maxqps"));
            int window_sec = std::stoi(
                MprpcApplication::GetConfig().Load("ratelimit_windowsec").empty()
                ? "1" : MprpcApplication::GetConfig().Load("ratelimit_windowsec"));

            m_rate_limiter = std::make_unique<RateLimiter>(
                redis, max_qps, window_sec,
                RateLimitAlgorithm::TOKEN_BUCKET);
            LOG_INFO("RateLimiter initialized: max_qps=%d, window=%ds", max_qps, window_sec);
        }
    }

    // ... 原有代码 ...
}

// OnMessage() 中增加限流检查（在解析 header 之后，处理请求之前）:
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp)
{
    // ... 解析 rpc header，得到 service_name, method_name ...

    // ── 限流检查 ──
    if (m_rate_limiter)
    {
        std::string client_ip = conn->peerAddress().toIp();
        if (!m_rate_limiter->allow(service_name, method_name, client_ip))
        {
            LOG_WARN("Rate limit exceeded: %s/%s from %s",
                     service_name.c_str(), method_name.c_str(), client_ip.c_str());
            sendErrorResponse(conn, "rate limit exceeded, try again later");
            return;
        }
    }

    // ... 继续原有处理逻辑 ...
}
```

### 3.7 限流在 Consumer 侧的集成

在 `mprpcchannel.cc` 的 `CallMethod()` 中，在发起 RPC 调用前进行客户端限流：

```cpp
// MprpcChannel 类增加成员:
// std::unique_ptr<RateLimiter> m_client_rate_limiter;

void MprpcChannel::CallMethod(...)
{
    // ── 客户端限流 ──
    if (m_client_rate_limiter)
    {
        if (!m_client_rate_limiter->allow(service_name, method_name, ""))
        {
            std::string err = "client rate limit exceeded for " + method_path;
            LOG_ERROR("%s", err.c_str());
            static_cast<MprpcController*>(controller)->SetFailed(err);
            return;
        }
    }

    // ... 原有逻辑 ...
}
```

---

## 4. 分布式锁（Distributed Lock）

### 4.1 为什么需要分布式锁

在分布式 RPC 框架中，以下场景需要分布式锁：

1. **幂等服务防重入**：防止同一个请求被 Consumer 重试时在 Provider 侧执行多次
2. **共享资源互斥**：多个 Consumer 同时对同一资源（如数据库记录、文件）进行操作
3. **定时任务防重复**：在集群中只让一个节点执行定时任务
4. **全局 ID 生成**：确保全局唯一 ID 生成的互斥性

### 4.2 分布式锁的设计原则

一个可靠的分布式锁需要满足以下条件：

| 原则 | 说明 |
|------|------|
| **互斥性** | 同一时刻只有一个客户端持有锁 |
| **安全性** | 不会产生死锁（最终一定会释放） |
| **容错性** | Redis 节点故障不影响锁的可用性（Redlock） |
| **可重入性** | 同一客户端可以重复获取同一把锁 |
| **高性能** | 加锁/解锁操作延迟低 |

### 4.3 基于 SETNX + EXPIRE 的简单锁

**原理**：利用 `SET key value NX EX ttl` 的原子性。

```
SET lock_key unique_id NX EX 10
          │
          ├── 成功 (返回 OK) → 获得锁
          └── 失败 (返回 nil) → 锁被占用
```

**解锁**：使用 Lua 脚本保证原子性（先校验 value 再删除，防止释放别人的锁）：

```lua
if redis.call('get', KEYS[1]) == ARGV[1] then
    return redis.call('del', KEYS[1])
else
    return 0
end
```

### 4.4 Redlock 算法

**背景**：单节点 Redis 锁存在单点故障风险。如果主节点宕机，从节点未同步锁信息，可能导致多个客户端同时持有锁。

**Redlock 原理**（由 Redis 作者 antirez 提出）：

1. 获取当前时间戳 `T1`
2. 依次向 N 个独立的 Redis 节点（通常 N=5）发起 SET NX EX 请求，每个请求的超时时间远小于锁的 TTL
3. 计算获取锁的总耗时 `elapsed = T2 - T1`
4. 如果成功加锁的节点数 >= N/2 + 1（即多数），且 `elapsed < lock_ttl`，则锁获取成功
5. 锁的实际有效时间 = `lock_ttl - elapsed`
6. 如果锁获取失败，向所有节点发起解锁请求

```
Client
  │
  ├──→ Redis-A: SETNX lock uid NX EX 10 → OK
  ├──→ Redis-B: SETNX lock uid NX EX 10 → OK
  ├──→ Redis-C: SETNX lock uid NX EX 10 → OK
  ├──→ Redis-D: SETNX lock uid NX EX 10 → FAIL (超时)
  └──→ Redis-E: SETNX lock uid NX EX 10 → OK
            │
            多数 = 4 >= 3 (5/2+1)，且耗时 < 10s → 锁获取成功
```

**本项目实现**：

```cpp
// src/include/distlock.h
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "redisutil.h"

class RedLock {
public:
    RedLock(std::vector<std::shared_ptr<RedisClient>> redis_nodes,
            int quorum = 0, int retry_count = 3, int retry_delay_ms = 200);

    // 加锁
    // lock_ttl_ms: 锁自动释放时间（毫秒）
    bool lock(const std::string& resource, const std::string& value,
              int lock_ttl_ms = 10000);

    // 解锁
    bool unlock(const std::string& resource, const std::string& value);

    // 是否为可重入锁
    void setReentrant(bool reentrant) { m_reentrant = reentrant; }

private:
    std::vector<std::shared_ptr<RedisClient>> m_redis_nodes;
    int m_quorum;             // 法定多数节点数
    int m_retry_count;        // 重试次数
    int m_retry_delay_ms;     // 重试间隔

    bool m_reentrant = false;

    bool lockNode(RedisClient& redis, const std::string& resource,
                  const std::string& value, int ttl_ms);
    bool unlockNode(RedisClient& redis, const std::string& resource,
                    const std::string& value);
};
```

```cpp
// src/distlock.cc
#include "distlock.h"
#include "logger.h"

RedLock::RedLock(std::vector<std::shared_ptr<RedisClient>> redis_nodes,
                 int quorum, int retry_count, int retry_delay_ms)
    : m_redis_nodes(std::move(redis_nodes))
    , m_retry_count(retry_count)
    , m_retry_delay_ms(retry_delay_ms)
{
    if (quorum <= 0)
    {
        // 默认法定多数 = N/2 + 1
        m_quorum = m_redis_nodes.size() / 2 + 1;
    }
    else
    {
        m_quorum = quorum;
    }
    LOG_INFO("RedLock initialized: nodes=%zu, quorum=%d",
             m_redis_nodes.size(), m_quorum);
}

bool RedLock::lockNode(RedisClient& redis, const std::string& resource,
                       const std::string& value, int ttl_ms)
{
    return redis.Lock(resource, value, ttl_ms / 1000 + 1);
}

bool RedLock::unlockNode(RedisClient& redis, const std::string& resource,
                         const std::string& value)
{
    return redis.Unlock(resource, value);
}

bool RedLock::lock(const std::string& resource, const std::string& value,
                   int lock_ttl_ms)
{
    int64_t start = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (int retry = 0; retry < m_retry_count; ++retry)
    {
        int successful = 0;

        for (auto& node : m_redis_nodes)
        {
            if (lockNode(*node, resource, value, lock_ttl_ms))
            {
                successful++;
            }
        }

        // 检查是否获得多数节点
        int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - start;

        if (successful >= m_quorum && elapsed < lock_ttl_ms)
        {
            LOG_INFO("RedLock acquired: resource=%s, nodes=%d/%zu",
                     resource.c_str(), successful, m_redis_nodes.size());
            return true;
        }

        // 获取失败，解锁所有已加锁节点
        for (auto& node : m_redis_nodes)
        {
            unlockNode(*node, resource, value);
        }

        if (retry < m_retry_count - 1)
        {
            usleep(m_retry_delay_ms * 1000);
        }
    }

    LOG_WARN("RedLock failed: resource=%s, retries=%d",
             resource.c_str(), m_retry_count);
    return false;
}

bool RedLock::unlock(const std::string& resource, const std::string& value)
{
    bool all_ok = true;
    for (auto& node : m_redis_nodes)
    {
        if (!unlockNode(*node, resource, value))
        {
            all_ok = false;
        }
    }
    return all_ok;
}
```

### 4.5 Watchdog 机制（自动续期）

**问题**：锁的 TTL 过期后如果业务还没执行完，锁会被自动释放，导致其他客户端同时获取锁。

**解决方案**：Watchdog（看门狗）后台线程在锁持有期间定期续期。

```cpp
// 在 RedLock 中增加
class RedLock {
public:
    // ... 原有接口 ...

    // 启动 watchdog（持有锁后调用）
    // resource, value: 标识锁
    // extend_interval_ms: 续期间隔(默认 TTL/3)
    // lock_ttl_ms: 锁 TTL
    void startWatchdog(const std::string& resource, const std::string& value,
                       int lock_ttl_ms);
    void stopWatchdog();

private:
    std::atomic<bool> m_watchdog_running{false};
    std::thread m_watchdog_thread;

    void watchdogLoop(const std::string resource, const std::string value,
                      int lock_ttl_ms, int extend_interval_ms);
};

void RedLock::startWatchdog(const std::string& resource, const std::string& value,
                            int lock_ttl_ms)
{
    if (m_watchdog_running.exchange(true)) return;

    int extend_interval_ms = lock_ttl_ms / 3;
    m_watchdog_thread = std::thread([this, resource, value, lock_ttl_ms, extend_interval_ms]() {
        watchdogLoop(resource, value, lock_ttl_ms, extend_interval_ms);
    });
    m_watchdog_thread.detach();
}

void RedLock::stopWatchdog()
{
    m_watchdog_running = false;
}

void RedLock::watchdogLoop(const std::string resource, const std::string value,
                           int lock_ttl_ms, int extend_interval_ms)
{
    LOG_INFO("Watchdog started for resource=%s, interval=%dms",
             resource.c_str(), extend_interval_ms);

    while (m_watchdog_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(extend_interval_ms));

        if (!m_watchdog_running) break;

        // Lua 脚本：校验 value 一致则续期
        const char* script =
            "if redis.call('get', KEYS[1]) == ARGV[1] then "
            "    return redis.call('expire', KEYS[1], ARGV[2]) "
            "else "
            "    return 0 "
            "end";

        for (auto& node : m_redis_nodes)
        {
            std::vector<std::string> keys = {resource};
            std::vector<std::string> args = {value, std::to_string(lock_ttl_ms / 1000)};
            node->Eval(script, keys, args);
        }

        LOG_INFO("Watchdog extended lock %s", resource.c_str());
    }

    LOG_INFO("Watchdog stopped for resource=%s", resource.c_str());
}
```

### 4.6 分布式锁在项目中的使用场景

#### 场景1：幂等性控制

在 Provider 侧防止重试导致的重复执行：

```cpp
// mprpcprovider.cc OnMessage() 中
void RpcProvider::OnMessage(...)
{
    // ... 解析 header 得到 service_name, method_name ...

    // 生成幂等性 Key
    // 假设请求中包含 request_id（由 Consumer 生成）
    // std::string request_id = ...从请求中提取...;
    std::string idempotent_key = "mprpc:idempotent:"
                                 + service_name + ":" + method_name
                                 + ":" + request_id;

    // 尝试加锁（TTL = 10s，超过此时间认为请求已超时）
    auto& dist_lock = getDistLock();
    std::string lock_value = std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch().count());

    if (!dist_lock.lock(idempotent_key, lock_value, 10000))
    {
        // 重复请求，直接返回上一次的结果（从缓存读取）
        LOG_WARN("Duplicate request detected: %s", idempotent_key.c_str());
        // 返回缓存的结果...
        return;
    }

    // 继续处理请求...

    // 处理完成后释放锁
    dist_lock.unlock(idempotent_key, lock_value);
}
```

#### 场景2：定时任务防重复

```cpp
// 在服务启动时，尝试获取定时任务锁
void RpcProvider::Run()
{
    // ... 原有代码 ...

    // 尝试获取定时任务调度权
    auto& dist_lock = getDistLock();
    std::string scheduler_lock_key = "mprpc:scheduler:cleanup_task";
    std::string instance_id = m_ip + ":" + std::to_string(m_port);

    if (dist_lock.lock(scheduler_lock_key, instance_id, 60000))
    {
        dist_lock.startWatchdog(scheduler_lock_key, instance_id, 60000);
        LOG_INFO("This instance (%s) acquired scheduler lock", instance_id.c_str());
        // 启动定时任务线程
        startSchedulerTask();
    }

    // ... 原有代码 ...
}
```

---

## 5. 高可用方案（High Availability）

### 5.1 Redis 单节点问题

单节点 Redis 部署的风险：

| 风险 | 后果 |
|------|------|
| 单点故障 | Redis 宕机 → 所有依赖 Redis 的功能失效 |
| 网络分区 | Consumer 无法连接 Redis → 限流/锁不可用 |
| 资源瓶颈 | 单节点 CPU/内存/网络达到上限 |
| 数据丢失 | 没有持久化或持久化策略不当导致数据丢失 |

### 5.2 方案一：Redis Sentinel（推荐用于本项目）

#### 5.2.1 Architecture

```
        ┌──────────────┐
        │   Sentinel-1 │  (监控、通知、故障转移)
        └──────┬───────┘
               │
    ┌──────────┼──────────┐
    │          │          │
    ▼          ▼          ▼
┌───────┐ ┌───────┐ ┌───────┐
|Master | |Slave-1| |Slave-2|
| 主节点  | | 从节点  | | 从节点  |
└───────┘ └───────┘ └───────┘
    ↑
    │ 写
 Consumer
```

**核心流程**：

1. **监控**：Sentinel 每 10s（默认）向所有节点发送 PING
2. **主观下线**（SDOWN）：单个 Sentinel 认为节点不可达（`down-after-milliseconds`）
3. **客观下线**（ODOWN）：多数 Sentinel 认为节点不可达
4. **Leader 选举**：Sentinel 之间选举一个 Leader 执行故障转移
5. **故障转移**：Leader 从 Slave 中选一个升级为 Master
6. **通知**：通知所有 Client 新的 Master 地址

#### 5.2.2 Sentinel 客户端实现

```cpp
// src/include/redissentinel.h
#pragma once
#include "redisutil.h"
#include <vector>
#include <string>
#include <thread>
#include <atomic>

struct SentinelNode {
    std::string ip;
    int port;
};

struct RedisInstance {
    std::string ip;
    int port;
    std::string role;  // "master" or "slave"
};

class RedisSentinel {
public:
    RedisSentinel(const std::vector<SentinelNode>& sentinels,
                  const std::string& master_name,
                  const std::string& password = "",
                  int connect_timeout_sec = 3);

    ~RedisSentinel();

    // 获取当前 Master 连接
    std::shared_ptr<RedisClient> getMasterConnection();

    // 获取 Slave 连接（用于读操作）
    std::shared_ptr<RedisClient> getSlaveConnection();

    // 手动触发 Master 发现
    bool discoverMaster();

    // Sentinel 是否可用
    bool isAvailable() const { return m_available; }

private:
    std::vector<SentinelNode> m_sentinels;
    std::string m_master_name;
    std::string m_password;
    int m_connect_timeout_sec;

    std::shared_ptr<RedisClient> m_master_conn;
    std::shared_ptr<RedisClient> m_slave_conn;
    mutable std::mutex m_mtx;
    std::atomic<bool> m_available{false};

    // 后台监控线程
    std::thread m_monitor_thread;
    std::atomic<bool> m_running{false};

    // 从 Sentinel 查询 Master 地址
    bool queryMasterFromSentinels(std::string& out_ip, int& out_port);

    // 后台定期检查 Master 健康状态
    void monitorLoop();
};
```

```cpp
// src/redissentinel.cc
#include "redissentinel.h"
#include "logger.h"
#include <hiredis/hiredis.h>

RedisSentinel::RedisSentinel(const std::vector<SentinelNode>& sentinels,
                             const std::string& master_name,
                             const std::string& password,
                             int connect_timeout_sec)
    : m_sentinels(sentinels)
    , m_master_name(master_name)
    , m_password(password)
    , m_connect_timeout_sec(connect_timeout_sec)
{
    if (discoverMaster())
    {
        m_available = true;
        // 启动后台监控
        m_running = true;
        m_monitor_thread = std::thread([this]() { monitorLoop(); });
        m_monitor_thread.detach();
    }
}

RedisSentinel::~RedisSentinel()
{
    m_running = false;
}

bool RedisSentinel::queryMasterFromSentinels(std::string& out_ip, int& out_port)
{
    for (const auto& sentinel : m_sentinels)
    {
        struct timeval tv = {m_connect_timeout_sec, 0};
        redisContext* ctx = redisConnectWithTimeout(
            sentinel.ip.c_str(), sentinel.port, tv);

        if (!ctx || ctx->err)
        {
            if (ctx)
            {
                LOG_WARN("Failed to connect to sentinel %s:%d: %s",
                         sentinel.ip.c_str(), sentinel.port, ctx->errstr);
                redisFree(ctx);
            }
            continue;
        }

        // 执行 SENTINEL get-master-addr-by-name
        redisReply* reply = (redisReply*)redisCommand(
            ctx, "SENTINEL get-master-addr-by-name %s", m_master_name.c_str());

        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 2)
        {
            out_ip = reply->element[0]->str;
            out_port = std::stoi(reply->element[1]->str);
            freeReply(reply);
            redisFree(ctx);

            LOG_INFO("Discovered Redis master %s:%d from sentinel %s:%d",
                     out_ip.c_str(), out_port,
                     sentinel.ip.c_str(), sentinel.port);
            return true;
        }

        if (reply) freeReply(reply);
        redisFree(ctx);
    }

    LOG_ERROR("Failed to discover Redis master from any sentinel");
    return false;
}

bool RedisSentinel::discoverMaster()
{
    std::string ip;
    int port = 0;
    if (!queryMasterFromSentinels(ip, port))
    {
        return false;
    }

    auto conn = std::make_shared<RedisClient>();
    if (!conn->Connect(ip, port, m_password, m_connect_timeout_sec))
    {
        LOG_ERROR("Failed to connect to Redis master %s:%d", ip.c_str(), port);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mtx);
    m_master_conn = conn;
    LOG_INFO("Redis master set to %s:%d", ip.c_str(), port);
    return true;
}

std::shared_ptr<RedisClient> RedisSentinel::getMasterConnection()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_master_conn && m_master_conn->IsConnected())
    {
        return m_master_conn;
    }
    return nullptr;
}

void RedisSentinel::monitorLoop()
{
    while (m_running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto conn = getMasterConnection();
        if (conn && conn->IsConnected())
        {
            // 健康检查：执行 PING
            redisReply* reply = nullptr;
            // 简化：直接检查 IsConnected()
            continue;
        }

        // Master 不可用，重新发现
        LOG_WARN("Redis master appears down, rediscovering...");
        if (discoverMaster())
        {
            LOG_INFO("Redis master rediscovered successfully");
        }
        else
        {
            LOG_ERROR("Redis master rediscovery failed, will retry");
        }
    }
}
```

### 5.3 方案二：Redis Cluster

#### 5.3.1 Architecture

```
┌──────────┐  ┌──────────┐  ┌──────────┐
|  Node-1  |  |  Node-2  |  |  Node-3  |
| Master A |  | Master B |  | Master C |
| Slave  A1|  | Slave  B1|  | Slave  C1|
└──────────┘  └──────────┘  └──────────┘
      │             │             │
      └─────────────┼─────────────┘
                    │
             Consumer (通过 CRC16 路由)
```

**数据分片**：

- 整个 keyspace 被分为 16384 个 hash slot
- `CRC16(key) % 16384` 决定 key 所属的 slot
- 每个节点负责一部分 slot

```
计算: CRC16("mprpc:service:/UserService/Login") % 16384 = 12345
路由: Node-2 (负责 slots 8192~12287)
```

#### 5.3.2 Cluster 客户端实现

```cpp
// src/include/rediscluster.h
#pragma once
#include "redisutil.h"
#include <vector>
#include <map>
#include <thread>
#include <atomic>

struct ClusterNode {
    std::string id;
    std::string ip;
    int port;
    std::string role;       // "master" or "slave"
    std::string master_id;  // 如果为 slave，记录 master ID
    std::vector<int> slots; // 负责的 slot 范围
};

class RedisCluster {
public:
    RedisCluster(const std::vector<std::pair<std::string, int>>& seed_nodes,
                 const std::string& password = "",
                 int connect_timeout_sec = 3);
    ~RedisCluster();

    // 根据 key 路由到正确的节点
    std::shared_ptr<RedisClient> getConnection(const std::string& key);

    // 重新发现集群拓扑
    bool refreshCluster();

private:
    std::vector<std::pair<std::string, int>> m_seed_nodes;
    std::string m_password;
    int m_connect_timeout_sec;

    // slot → node 映射表
    std::map<int, std::shared_ptr<RedisClient>> m_slot_map;
    // 所有节点的连接
    std::vector<std::shared_ptr<RedisClient>> m_all_nodes;

    mutable std::mutex m_mtx;
    std::atomic<bool> m_initialized{false};

    // 从节点获取集群信息
    bool fetchClusterInfo(const std::string& ip, int port);
    bool connectToNode(const std::string& ip, int port,
                       std::shared_ptr<RedisClient>& out_conn);

    // 处理 MOVED / ASK 重定向
    bool handleRedirect(redisReply* reply, const std::string& key);
};
```

### 5.4 项目中的高可用策略选择

| 维度 | Sentinel | Cluster |
|------|----------|---------|
| 节点数 | 1主N从 + 3 Sentinel | 至少 3主3从 |
| **数据量** | < 10GB | 10GB+ |
| **复杂度** | 低 | 高 |
| **一致性** | 最终一致性（异步复制） | 最终一致性 |
| **本项目推荐** | ⭐ **首选** | 数据量大时考虑 |

**本项目推荐 Sentinel 的原因**：
- 项目数据量小（主要是缓存和计数器），单节点足够
- Sentinel 配置简单，运维成本低
- 限流/锁等功能对数据分片无特殊需求
- Cluster 的 MOVED 重定向增加客户端复杂度

### 5.5 降级策略

当 Redis 不可用时，各功能应优雅降级：

```cpp
// 统一的 Redis 健康状态管理
class RedisHealth {
public:
    static RedisHealth& GetInstance();

    void markUnavailable() { m_available = false; }
    void markAvailable() { m_available = true; }
    bool isAvailable() const { return m_available; }

    // 统计连续失败次数，达到阈值后标记不可用
    void recordFailure()
    {
        m_consecutive_failures++;
        if (m_consecutive_failures >= m_failure_threshold)
        {
            markUnavailable();
            LOG_ERROR("Redis marked unavailable after %d consecutive failures",
                      m_consecutive_failures);
        }
    }

    void recordSuccess()
    {
        m_consecutive_failures = 0;
        if (!m_available)
        {
            markAvailable();
            LOG_INFO("Redis recovered, marked available");
        }
    }

private:
    std::atomic<bool> m_available{true};
    std::atomic<int> m_consecutive_failures{0};
    int m_failure_threshold = 3;
};
```

**各功能的降级行为**：

| 功能 | Redis 不可用时的行为 |
|------|-------------------|
| 服务发现缓存 | 降级为直接查询 ZK（原行为） |
| 熔断器状态持久化 | 降级为内存态熔断器（重启后重置） |
| 分布式限流 | **放行所有请求**（保护可用性 > 限流精度） |
| 分布式锁 | 降级为单机锁（仅本进程内互斥） |
| 实时监控指标 | 降级为日志记录 |

---

## 6. 淘汰策略（Eviction Policy）

### 6.1 为什么需要淘汰策略

Redis 是内存数据库，内存是有限资源。当写入的数据超过 `maxmemory` 时，Redis 需要根据配置的淘汰策略（eviction policy）决定删除哪些数据。

### 6.2 Redis 8 种淘汰策略

| 策略 | 含义 | 本项目适用场景 |
|------|------|--------------|
| **noeviction** | 不淘汰，写入返回 OOM 错误 | ❌ 不推荐 |
| **allkeys-lru** | 对所有 key 按 LRU 淘汰 | ⭐ 服务发现缓存 |
| **allkeys-lfu** | 对所有 key 按 LFU 淘汰 | 🔸 监控计数器 |
| **volatile-lru** | 仅对设置了 TTL 的 key 按 LRU 淘汰 | ⭐ 限流计数器 |
| **volatile-lfu** | 仅对设置了 TTL 的 key 按 LFU 淘汰 | 🔸 熔断器状态 |
| **allkeys-random** | 随机淘汰任意 key | ❌ 不推荐 |
| **volatile-random** | 仅对设置了 TTL 的 key 随机淘汰 | ❌ 不推荐 |
| **volatile-ttl** | 淘汰 TTL 最短的 key（即将过期的） | 🔸 限流计数器 |

### 6.3 LRU 近似算法

Redis 的 LRU 不是精确 LRU（不维护全量访问时间链表），而是**采样近似 LRU**：

1. 从数据库随机抽取 N 个 key（`maxmemory-samples`，默认 5）
2. 比较这些 key 的 `lru` 字段（24bit，记录上次访问的时间戳）
3. 淘汰其中空闲时间最长的那个
4. 重复直到内存低于 `maxmemory`

```
采样 N=5 个 key:
  key-a: 上次访问 100s 前  ← 空闲最久，淘汰
  key-b: 上次访问 30s 前
  key-c: 上次访问 5s 前
  key-d: 上次访问 60s 前
  key-e: 上次访问 2s 前

淘汰 key-a
```

**采样数对精度的影响**：

```
maxmemory-samples = 5  →  近似 LRU 淘汰效果 ≈ 真实 LRU 的 80%
maxmemory-samples = 10 →  近似 LRU 淘汰效果 ≈ 真实 LRU 的 95%
```

> ⚡ **性能权衡**：`maxmemory-samples` 越大，LRU 越精确，但淘汰耗时越长。

### 6.4 LFU（Least Frequently Used）

Redis 4.0+ 支持 LFU 淘汰，使用**莫里斯计数器**（Morris counter）近似统计访问频率：

- `lru` 字段（24bit）在 LFU 模式下拆分为：
  - 高 16 位：**上次衰减时间**（以分钟为单位的时间戳）
  - 低 8 位：**访问频次计数器**（对数增长）

```
LRU 模式下: |←────────── 24-bit LRU 时间戳 ──────────→|

LFU 模式下: |← 16-bit 衰减时间 →|← 8-bit 计数器 →|
```

**计数器增长**：对数增长，低频时增长快，高频时增长慢
```
访问 1 次 → 计数器 ≈ 5
访问 10 次 → 计数器 ≈ 22
访问 100 次 → 计数器 ≈ 70
访问 1000 次 → 计数器 ≈ 140
```

**计数器衰减**：每 `lfu-decay-time` 分钟减少一次频率值，确保"热数据"会随时间冷却。

### 6.5 针对本项目的淘汰策略配置建议

#### 配置文件示例

```conf
# Redis 内存管理
maxmemory=512mb
maxmemory-policy=allkeys-lru
maxmemory-samples=10

# LFU 衰减配置（仅 LFU 策略有效）
lfu-log-factor=10
lfu-decay-time=1
```

#### 各数据类型的最佳策略

| Redis 数据用途 | Key 模式 | 是否有 TTL | 推荐淘汰策略 | 理由 |
|---------------|----------|-----------|-------------|------|
| 服务发现缓存 | `mprpc:service:*` | 是 (30s) | volatile-lru | 有 TTL，LRU 自然淘汰冷服务 |
| 熔断器状态 | `mprpc:breaker:*` | 是 (60s) | volatile-lru | 有 TTL，熔断状态短暂 |
| 限流计数器 | `mprpc:ratelimit:*` | 是 (动态) | volatile-ttl | 优先淘汰即将过期的 |
| 监控指标 | `mprpc:metrics:*` | 是 (1h) | volatile-lru | 有 TTL，淘汰旧指标 |
| 分布式锁 | `mprpc:lock:*` | 是 (10s) | volatile-lru | 锁自动释放 |
| 幂等性 Key | `mprpc:idempotent:*` | 是 (30s) | volatile-ttl | 尽快淘汰过期 Key |
| 存活 Provider | `mprpc:live_providers` | 否 | allkeys-lru | 无 TTL，LRU 淘汰离线节点 |

> **推荐全局配置**：`maxmemory-policy=volatile-lru`
>
> 本项目所有 Redis Key 均设置了 TTL，使用 `volatile-lru` 可以在内存紧张时淘汰最不常用的"有过期时间的 Key"，未设置 TTL 的 Key（如果有）不会被误删。

### 6.6 内存预警监控

```cpp
// 在监控系统中定期检查 Redis 内存
void checkRedisMemory(RedisClient& redis)
{
    redisReply* reply = redisCommand(redis.getContext(), "INFO memory");
    if (!reply || reply->type != REDIS_REPLY_STRING) return;

    // 解析 INFO 输出
    // used_memory_human: 当前使用内存
    // maxmemory_human: 最大内存
    // mem_fragmentation_ratio: 碎片率

    // 如果内存使用率 > 80%，记录告警
    float usage_pct = (float)used_memory / maxmemory * 100;
    if (usage_pct > 80.0f)
    {
        LOG_WARN("Redis memory usage warning: %.1f%% (%s / %s)",
                 usage_pct, used_memory_human, maxmemory_human);
    }
}
```

---

## 7. 实时监控指标

### 7.1 指标分类与存储

| 指标类别 | 数据 | Redis 数据结构 | Key 模式 |
|---------|------|---------------|---------|
| **调用量** | 各方法的 QPS | String (INCR) | `mprpc:metrics:{method}:calls:{minute}` |
| **错误率** | 各方法的错误计数 | String (INCR) | `mprpc:metrics:{method}:errors:{minute}` |
| **延迟** | P50/P90/P99 延迟 | ZSet | `mprpc:metrics:{method}:latency` |
| **熔断器** | 各实例熔断器状态 | Hash | `mprpc:metrics:breakers` |
| **Redis 健康** | Redis 可用性/延迟 | String | `mprpc:metrics:redis_health` |
| **限流统计** | 被限流的请求数 | String (INCR) | `mprpc:metrics:{method}:blocked:{minute}` |

### 7.2 MetricsCollector 类

```cpp
// src/include/metricscollector.h
#pragma once
#include <memory>
#include <string>
#include "redisutil.h"

class MetricsCollector {
public:
    MetricsCollector(std::shared_ptr<RedisClient> redis);

    // 记录一次调用
    void recordCall(const std::string& service_name,
                    const std::string& method_name,
                    int64_t latency_ms,
                    bool is_error);

    // 记录熔断器状态变更
    void recordBreakerState(const std::string& cb_key,
                            const std::string& state);

    // 记录被限流的请求
    void recordBlocked(const std::string& service_name,
                       const std::string& method_name);

    // 获取当前分钟的时间戳 Key
    static std::string currentMinuteKey();

private:
    std::shared_ptr<RedisClient> m_redis;

    // 延迟采样：记录到 ZSet，并维护最多 1000 条
    void recordLatency(const std::string& method_path, int64_t latency_ms);
};
```

```cpp
// src/metricscollector.cc
#include "metricscollector.h"
#include <sstream>
#include <chrono>

MetricsCollector::MetricsCollector(std::shared_ptr<RedisClient> redis)
    : m_redis(redis) {}

std::string MetricsCollector::currentMinuteKey()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    struct tm* tm = localtime(&tt);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min);
    return buf;
}

void MetricsCollector::recordCall(const std::string& service_name,
                                  const std::string& method_name,
                                  int64_t latency_ms,
                                  bool is_error)
{
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string minute = currentMinuteKey();

    // 调用量
    m_redis->Incr("mprpc:metrics" + method_path + ":calls:" + minute);
    m_redis->Expire("mprpc:metrics" + method_path + ":calls:" + minute, 3600);

    if (is_error)
    {
        m_redis->Incr("mprpc:metrics" + method_path + ":errors:" + minute);
        m_redis->Expire("mprpc:metrics" + method_path + ":errors:" + minute, 3600);
    }

    // 延迟采样（1/10 概率采样，减少开销）
    if (rand() % 10 == 0)
    {
        recordLatency(method_path, latency_ms);
    }
}

void MetricsCollector::recordLatency(const std::string& method_path, int64_t latency_ms)
{
    std::string key = "mprpc:metrics" + method_path + ":latency";

    // 将延迟记录下来
    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    m_redis->ZAdd(key, static_cast<double>(latency_ms),
                  std::to_string(now_ms) + ":" + std::to_string(rand()));

    // 保持 ZSet 大小 <= 1000
    m_redis->ZRemRangeByRank(key, 0, -1001);
    m_redis->Expire(key, 3600);
}

void MetricsCollector::recordBreakerState(const std::string& cb_key,
                                          const std::string& state)
{
    m_redis->HSet("mprpc:metrics:breakers", cb_key, state);
}

void MetricsCollector::recordBlocked(const std::string& service_name,
                                     const std::string& method_name)
{
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string minute = currentMinuteKey();

    m_redis->Incr("mprpc:metrics" + method_path + ":blocked:" + minute);
    m_redis->Expire("mprpc:metrics" + method_path + ":blocked:" + minute, 3600);
}
```

### 7.3 集成到 CallMethod 中

```cpp
// mprpcchannel.cc CallMethod()
void MprpcChannel::CallMethod(...)
{
    auto call_start = std::chrono::steady_clock::now();
    bool is_error = false;

    // ... 原有调用逻辑 ...

    // 在 return 前记录指标
    auto call_end = std::chrono::steady_clock::now();
    int64_t latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        call_end - call_start).count();

    if (m_metrics)
    {
        bool had_error = static_cast<MprpcController*>(controller)->Failed();
        m_metrics->recordCall(service_name, method_name, latency_ms, had_error);
    }
}
```

---

## 8. 项目集成指南

### 8.1 依赖安装

```bash
# 安装 hiredis（Redis C 客户端）
sudo apt-get install libhiredis-dev

# 验证安装
pkg-config --cflags --libs hiredis
# 输出: -I/usr/include/hiredis -lhiredis
```

### 8.2 文件结构变更

```
src/
├── include/
│   ├── redisutil.h              # Redis 客户端封装（新增）
│   ├── redispool.h              # Redis 连接池（新增）
│   ├── redissentinel.h          # Redis Sentinel 支持（新增）
│   ├── rediscluster.h           # Redis Cluster 支持（新增，可选）
│   ├── ratelimiter.h            # 分布式限流器（新增）
│   ├── distlock.h               # 分布式锁（新增）
│   ├── metricscollector.h       # 监控指标收集器（新增）
│   ├── redishealth.h            # Redis 健康状态管理（新增）
│   └── ...                      # 原有头文件不变
│
├── redisutil.cc                 # Redis 客户端实现（新增）
├── redispool.cc                 # 连接池实现（新增）
├── redissentinel.cc             # Sentinel 实现（新增）
├── ratelimiter.cc               # 限流器实现（新增）
├── distlock.cc                  # 分布式锁实现（新增）
├── metricscollector.cc          # 监控收集器实现（新增）
│
├── mprpcchannel.cc              # 修改：增加 Redis 缓存 + 限流 + 指标
├── mprpcprovider.cc             # 修改：增加限流 + 分布式锁
├── mprpcapplication.cc          # 修改：增加 Redis 配置加载
├── mprpcconfig.cc               # 不变
└── ...
```

### 8.3 CMakeLists.txt 修改

```cmake
# src/CMakeLists.txt

# 新增源文件
set(SRC_LIST
    Logger.cc
    mprpcapplication.cc
    mprpcchannel.cc
    mprpcconfig.cc
    mprpccontroller.cc
    mprpcprovider.cc
    rpcerror.cc
    rpcheader.pb.cc
    zookeeperutil.cc
    redisutil.cc           # 新增
    redispool.cc           # 新增
    redissentinel.cc       # 新增
    ratelimiter.cc         # 新增
    distlock.cc            # 新增
    metricscollector.cc    # 新增
)

# 添加 hiredis 头文件搜索路径
include_directories(/usr/include/hiredis)

# 链接 hiredis 库
target_link_libraries(mprpc muduo_net muduo_base pthread zookeeper_mt hiredis)
```

### 8.4 配置文件扩展

```conf
# ── test.conf ──
# RPC Server Configuration
rpcserverip=127.0.0.1
rpcserverport=8000

# ZooKeeper Configuration
zookeeperip=127.0.0.1
zookeeperport=2181

# ── Redis Configuration ──
# Redis 基础配置
redisip=127.0.0.1
redisport=6379
redispassword=
redisconnectionpoolsize=8
redistimeoutsec=3

# 服务发现缓存 TTL（秒）
rediscachettl=30

# 熔断器状态持久化
redisbreakerpersist=true

# ── Sentinel 配置（可选） ──
# 启用 Sentinel 则注释掉上面 redisip/redisport
# redissentinel=true
# redissentinelmastername=mymaster
# redissentinelnodes=127.0.0.1:26379,127.0.0.2:26379,127.0.0.3:26379

# ── 限流配置 ──
# 限流算法: 0=固定窗口, 1=滑动窗口日志(ZSET), 2=滑动窗口计数器, 3=令牌桶, 4=漏桶
ratelimitalgorithm=3
# 每个窗口最大请求数（QPS 上限）
ratelimitmaxqps=1000
# 窗口大小（秒）
ratelimitwindowsec=1
# 是否在 Provider 侧启用限流
ratelimitenableprovider=true
# 是否在 Consumer 侧启用限流
ratelimitenableconsumer=false

# ── 分布式锁配置 ──
# 锁默认 TTL（毫秒）
lockdefaultttlms=10000
# 锁重试次数
lockretrycount=3
# 锁重试间隔（毫秒）
lockretrydelayms=200

# ── 监控指标配置 ──
# 是否启用实时监控指标
metricsenable=true
# 延迟采样率 (1/N)
metricslatencysamplerate=10
```

### 8.5 代码注入点总结

| 文件 | 改动点 | 注入内容 |
|------|-------|---------|
| `mprpcapplication.cc` | `Init()` 末尾 | 初始化 Redis 连接池和 Sentinel |
| `mprpcchannel.h` | 成员变量 | 增加 `RedisClient`, `RateLimiter`, `MetricsCollector` 成员 |
| `mprpcchannel.cc` | `discoverService()` | 增加 Redis 缓存读写逻辑 |
| `mprpcchannel.cc` | `CircuitBreaker` 类 | 增加 Redis 持久化熔断器状态 |
| `mprpcchannel.cc` | `CallMethod()` 开头 | 增加 Consumer 侧限流检查 |
| `mprpcchannel.cc` | `CallMethod()` 成功/失败点 | 增加熔断器状态持久化到 Redis |
| `mprpcchannel.cc` | `CallMethod()` 返回前 | 增加监控指标记录 |
| `mprpcprovider.h` | 成员变量 | 增加 `RateLimiter`, `RedLock`, `MetricsCollector` 成员 |
| `mprpcprovider.cc` | `Run()` 中 | 初始化限流器、分布式锁 |
| `mprpcprovider.cc` | `OnMessage()` 中 | 增加限流检查、幂等性控制 |
| `mprpcconfig.cc` | 不变 | 已支持键值对读取 |

### 8.6 服务发现缓存集成（mprpcchannel.cc）

```cpp
// mprpcchannel.h 新增成员
#include "redisutil.h"
#include "metricscollector.h"

class MprpcChannel : public google::protobuf::RpcChannel {
    // ... 原有 ...

    // 新增：
    std::shared_ptr<RedisClient> m_redis;
    int m_redis_cache_ttl = 30;
    bool m_breaker_persist = false;
    std::unique_ptr<RateLimiter> m_client_rate_limiter;
    std::unique_ptr<MetricsCollector> m_metrics;
};

// mprpcchannel.cc discoverService() 修改
std::string MprpcChannel::discoverService(const std::string& method_path)
{
    // ── 1. 优先从 Redis 缓存读取 ──
    if (m_redis && m_redis->IsConnected())
    {
        std::string cached = m_redis->Get("mprpc:service:" + method_path);
        if (!cached.empty())
        {
            // 缓存命中，解析 JSON 数组
            std::vector<std::string> instances = parseJsonArray(cached);
            if (!instances.empty())
            {
                // 过滤熔断实例
                std::vector<std::string> alive;
                {
                    std::lock_guard<std::mutex> lock(m_cb_mtx);
                    for (auto& addr : instances)
                    {
                        std::string cb_key = method_path + "@" + addr;
                        if (m_instance_breakers[cb_key].allowRequest())
                            alive.push_back(addr);
                    }
                }
                if (!alive.empty())
                {
                    LOG_INFO("Service discovery from Redis cache: %s", method_path.c_str());
                    return selectInstance(method_path, alive);
                }
            }
        }
    }

    // ── 2. Redis 未命中，从 ZK 读取（原有逻辑） ──
    // ... 原有 ZK 读取逻辑 ...

    // ── 3. 写入 Redis 缓存 ──
    if (!instances.empty() && m_redis && m_redis->IsConnected())
    {
        m_redis->SetEx("mprpc:service:" + method_path,
                       toJsonArray(instances),
                       m_redis_cache_ttl);
    }
}
```

### 8.7 熔断器状态持久化

```cpp
// CircuitBreaker 构造函数增加 Redis 恢复逻辑
MprpcChannel::CircuitBreaker::CircuitBreaker(
    std::shared_ptr<RedisClient> redis,
    const std::string& cb_key,
    int threshold, int timeout_sec)
    : m_threshold(threshold), m_timeout_sec(timeout_sec),
      m_failure_count(0), m_state(State::CLOSED),
      m_redis(redis), m_cb_key(cb_key)
{
    if (m_redis && !cb_key.empty())
    {
        // 从 Redis 恢复状态
        std::string state = m_redis->HGet(
            "mprpc:breaker:" + cb_key, "state");
        if (state == "OPEN")
        {
            m_state = State::OPEN;
            std::string last_failure = m_redis->HGet(
                "mprpc:breaker:" + cb_key, "last_failure");
            if (!last_failure.empty())
            {
                m_last_failure_time = std::chrono::steady_clock::time_point(
                    std::chrono::seconds(std::stoll(last_failure)));
            }
            std::string count = m_redis->HGet(
                "mprpc:breaker:" + cb_key, "failure_count");
            if (!count.empty())
            {
                m_failure_count = std::stoi(count);
            }
            LOG_INFO("Circuit breaker restored from Redis: %s -> OPEN (failures=%d)",
                     cb_key.c_str(), m_failure_count);
        }
    }
}

// onFailure() 增加 Redis 持久化
void MprpcChannel::CircuitBreaker::onFailure()
{
    m_failure_count++;
    m_last_failure_time = std::chrono::steady_clock::now();

    if (m_failure_count >= m_threshold)
    {
        LOG_ERROR("Circuit breaker OPEN (threshold=%d)", m_threshold);
        m_state = State::OPEN;
    }

    // 持久化到 Redis
    if (m_redis && !m_cb_key.empty())
    {
        std::string redis_key = "mprpc:breaker:" + m_cb_key;
        m_redis->HSet(redis_key, "state",
                      m_state == State::OPEN ? "OPEN" : "CLOSED");
        m_redis->HSet(redis_key, "failure_count",
                      std::to_string(m_failure_count));
        m_redis->HSet(redis_key, "last_failure",
                      std::to_string(
                          std::chrono::duration_cast<std::chrono::seconds>(
                              m_last_failure_time.time_since_epoch()).count()));
        m_redis->Expire(redis_key, 60);
    }
}

void MprpcChannel::CircuitBreaker::onSuccess()
{
    m_failure_count = 0;
    if (m_state == State::HALF_OPEN)
    {
        LOG_INFO("Circuit breaker CLOSED (recovered)");
        m_state = State::CLOSED;
    }

    // Redis 中清除熔断状态
    if (m_redis && !m_cb_key.empty())
    {
        std::string redis_key = "mprpc:breaker:" + m_cb_key;
        m_redis->HSet(redis_key, "state", "CLOSED");
        m_redis->HSet(redis_key, "failure_count", "0");
        m_redis->Expire(redis_key, 60);
    }
}
```

### 8.8 MprpcChannel 构造函数初始化 Redis

```cpp
// mprpcchannel.cc 构造函数
MprpcChannel::MprpcChannel() : m_zk_available(true)
{
    // 原有 ZK 初始化
    if (m_zkclient.Start())
    {
        LOG_INFO("MprpcChannel: ZKClient initialized successfully");
    }
    else
    {
        m_zk_available = false;
        LOG_ERROR("MprpcChannel: ZKClient init failed, will use cached addresses");
    }

    // ── 新增：Redis 初始化 ──
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
            LOG_INFO("MprpcChannel: Redis initialized at %s:%s",
                     redis_ip.c_str(), redis_port.c_str());
        }
        else
        {
            LOG_ERROR("MprpcChannel: Redis init failed, degraded mode");
        }

        // 限流器初始化
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

        // 监控指标初始化
        std::string enable_metrics = config.Load("metricsenable");
        if (enable_metrics == "true")
        {
            m_metrics = std::make_unique<MetricsCollector>(m_redis);
            LOG_INFO("MprpcChannel: Metrics collector initialized");
        }
    }
}
```

### 8.9 autobuild.sh 更新

```bash
#!/bin/bash

# ... 原有 muduo 安装和 protobuf 生成逻辑 ...

# 安装 Redis 依赖
echo "Installing hiredis..."
sudo apt-get install -y libhiredis-dev

# 检查 hiredis 安装
if ! pkg-config --exists hiredis 2>/dev/null; then
    echo "hiredis not found via pkg-config, checking manually..."
    if [ ! -f "/usr/include/hiredis/hiredis.h" ]; then
        echo "ERROR: hiredis not installed. Please run: sudo apt-get install libhiredis-dev"
        exit 1
    fi
fi

# ... 原有 CMake 编译逻辑 ...
```

---

## 9. 数据流总图

### 9.1 完整架构总图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            Consumer (MprpcChannel)                      │
│                                                                         │
│  CallMethod()                                                           │
│    │                                                                    │
│    ├── ① 客户端限流检查 (RateLimiter) ──── 超过阈值 → 返回错误            │
│    │                                                                    │
│    ├── ② 服务发现                                                       │
│    │    ├── Redis 缓存命中? → 直接返回实例列表                           │
│    │    └── Redis 未命中 → ZK 查询 → 写入 Redis 缓存                    │
│    │                                                                    │
│    ├── ③ 熔断器检查                                                     │
│    │    ├── 内存熔断器状态                                               │
│    │    └── Redis 持久化状态恢复                                         │
│    │                                                                    │
│    ├── ④ 选路 (Round-Robin LB)                                          │
│    │                                                                    │
│    ├── ⑤ 发起 RPC 调用（TCP 连接）                                      │
│    │    ├── 成功 → 熔断器 onSuccess() + 持久化到 Redis                   │
│    │    └── 失败 → 熔断器 onFailure() + 持久化到 Redis + 重试            │
│    │                                                                    │
│    └── ⑥ 记录监控指标 (MetricsCollector)                                │
│         ├── 调用量 (INCR)                                               │
│         ├── 错误率 (INCR)                                               │
│         ├── 延迟采样 (ZADD)                                             │
│         └── 熔断器状态 (HSET)                                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                     │ RPC 请求
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                            Provider (RpcProvider)                       │
│                                                                         │
│  OnMessage()                                                            │
│    │                                                                    │
│    ├── ① 解析 RPC Header (service_name, method_name)                   │
│    │                                                                    │
│    ├── ② 分布式限流检查 (RateLimiter) ──── 超过阈值 → 返回限流错误        │
│    │                                                                    │
│    ├── ③ 幂等性检查 (Distributed Lock)                                  │
│    │    ├── 重复请求 → 返回缓存结果                                      │
│    │    └── 首次请求 → 继续处理                                          │
│    │                                                                    │
│    ├── ④ 调用业务逻辑 (CallMethod)                                      │
│    │                                                                    │
│    ├── ⑤ 释放幂等性锁 (Unlock)                                          │
│    │                                                                    │
│    ├── ⑥ 返回响应                                                       │
│    │                                                                    │
│    └── ⑦ 记录监控指标 (MetricsCollector)                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────┐
              │           Redis Server            │
              │                                   │
              │  ┌───────────────────────────┐   │
              │  │  Key 空间                    │   │
              │  │                           │   │
              │  │  mprpc:service:*          │ ← 服务发现缓存 (STRING, TTL 30s)     │
              │  │  mprpc:breaker:*          │ ← 熔断器状态 (HASH, TTL 60s)        │
              │  │  mprpc:ratelimit:*        │ ← 限流计数器 (STRING/ZSET, TTL 动态) │
              │  │  mprpc:lock:*             │ ← 分布式锁 (STRING, TTL 10s)        │
              │  │  mprpc:idempotent:*       │ ← 幂等性 Key (STRING, TTL 30s)      │
              │  │  mprpc:metrics:*          │ ← 监控指标 (STRING/ZSET, TTL 1h)    │
              │  │  mprpc:live_providers     │ ← 存活 Provider (SET)              │
              │  │  mprpc:token_bucket:*     │ ← 令牌桶状态 (HASH, TTL 24h)        │
              │  └───────────────────────────┘   │
              │                           │
              │  淘汰策略: volatile-lru      │
              │  maxmemory: 512mb           │
              └──────────────────────────────────┘
                                     │
            ┌────────────────────────┼────────────────────────┐
            │                        │                        │
            ▼                        ▼                        ▼
    ┌──────────────┐        ┌──────────────┐        ┌──────────────┐
    │  Zookeeper    │        │  Provider 1  │        │  Provider 2  │
    │  服务注册中心  │        │ 127.0.0.1:8000│        │127.0.0.1:8001│
    └──────────────┘        └──────────────┘        └──────────────┘
```

### 9.2 Redis Key 完整清单

| Key 模式 | 类型 | TTL | 用途 |
|----------|------|-----|------|
| `mprpc:service:/{svc}/{method}` | STRING (JSON) | 30s (可配) | 服务发现缓存 |
| `mprpc:breaker:instance:{method}@{ip}:{port}` | HASH | 60s | 熔断器状态持久化 |
| `mprpc:ratelimit:{svc}:{method}:{client}:fixed` | STRING | 窗口+1s | 固定窗口限流 |
| `mprpc:ratelimit:{svc}:{method}:{client}:sliding` | ZSET | 窗口+1s | 滑动窗口限流 |
| `mprpc:ratelimit:{svc}:{method}:{client}:sub:{idx}` | STRING | 窗口*2 | 滑动窗口子计数 |
| `mprpc:ratelimit:{svc}:{method}:{client}:token` | HASH | 86400s | 令牌桶状态 |
| `mprpc:ratelimit:{svc}:{method}:{client}:leaky` | LIST | 3600s | 漏桶状态 |
| `mprpc:lock:{resource}` | STRING | 10s (可配) | 分布式锁 |
| `mprpc:idempotent:{svc}:{method}:{req_id}` | STRING | 30s | 幂等性控制 |
| `mprpc:metrics/{svc}/{method}:calls:{minute}` | STRING | 1h | 调用量计数 |
| `mprpc:metrics/{svc}/{method}:errors:{minute}` | STRING | 1h | 错误计数 |
| `mprpc:metrics/{svc}/{method}:latency` | ZSET | 1h | 延迟采样 |
| `mprpc:metrics/{svc}/{method}:blocked:{minute}` | STRING | 1h | 限流拦截计数 |
| `mprpc:metrics:breakers` | HASH | 无 | 全局熔断器状态 |
| `mprpc:live_providers` | SET | 无 | 存活 Provider 集合 |
| `mprpc:scheduler:{task_name}` | STRING | TTL 看门狗 | 定时任务调度锁 |

---

## 10. 收益总结

### 10.1 功能矩阵

| 维度 | 改造前 | 改造后 |
|------|--------|--------|
| **服务发现** | 每次调用查 ZK，ZK 压力大 | Redis 缓存 99% 读请求，ZK 压力降低 1000 倍 |
| **ZK 故障容忍** | Consumer 重启后无法发现服务 | Redis 缓存持久化实例列表，重启后可继续运行 |
| **熔断器持久化** | Consumer 重启后熔断器重置为 CLOSE | 从 Redis 恢复熔断状态，直接跳过故障实例 |
| **流量控制** | 无全局限流，突发流量可打垮 Provider | 分布式令牌桶限流 + 滑动窗口，保护 Provider |
| **资源互斥** | 无分布式锁，竞态条件可能导致数据不一致 | Redlock 分布式锁 + Watchdog 自动续期 |
| **幂等性** | 重试可能导致重复执行 | 基于分布式锁的幂等性控制 |
| **可观测性** | 逐台 SSH grep 日志排障 | Redis 实时指标：QPS、错误率、P99 延迟、熔断器状态 |
| **Redis 高可用** | 无 | Sentinel 自动故障转移 + 连接池 + 自动重连 |
| **内存安全** | 无 | volatile-lru 淘汰策略 + 内存预警 |
| **水平扩展** | Consumer 数量受限 | Redis 缓存层解耦，Consumer 可水平扩展到 1000+ |

### 10.2 可靠性提升

```
ZK 故障场景下的服务可用性:

改造前: ZK 宕机 → 所有 Consumer 无法发现服务 → 服务完全不可用
        可用性 = ZK 可用性 (99.9%)

改造后: ZK 宕机 → Redis 缓存提供服务实例列表 → 服务继续可用
        可用性 = 1 - (1-ZK_avail) * (1-Redis_avail)
               = 1 - 0.001 * 0.001 = 99.9999%
```

---

## 11. 注意事项

### 11.1 架构原则

1. **Redis 不是 ZK 的替代品**
   - ZK：服务注册（写路径）、临时节点、Watcher 通知
   - Redis：缓存（读路径）、限流、锁、监控
   - 两者互补，ZK 负责一致性协调，Redis 负责性能加速

2. **数据一致性模型**
   - Redis 缓存与 ZK 之间：**最终一致性**（TTL 过期后重新从 ZK 加载）
   - 对于服务发现场景，30s 的最终一致性窗口完全可以接受
   - 分布式锁依赖 Redis，极端情况下锁可能失效，业务应该设计为幂等

3. **降级策略**
   - Redis 不可用时，所有功能必须优雅降级
   - 限流降级为**放行**（保可用性 > 限流精度）
   - 锁降级为**跳过**（业务方自行保证幂等）
   - 缓存降级为**直连 ZK**（与原行为一致）

### 11.2 线程安全

- `redisContext`（hiredis）**非线程安全**
- 多线程场景必须使用连接池，每个线程取独立的连接
- `mprpcchannel.cc` 的回调在 muduo 的 IO 线程中执行，需要通过 `RedisConnectionPool` 获取连接

### 11.3 生产环境推荐配置

```conf
# maxmemory 设置为物理内存的 50%~70%，留足给操作系统和应用
maxmemory=1gb
maxmemory-policy=volatile-lru
maxmemory-samples=10

# 持久化（保证重启不丢锁/熔断器状态）
save 900 1       # 15分钟内有1个key变化
save 300 10      # 5分钟内有10个key变化
save 60 10000    # 1分钟内有10000个key变化

# 慢查询日志（调试用）
slowlog-log-slower-than 10000   # 10ms
slowlog-max-len 128

# 连接数（根据 Consumer 规模调整）
maxclients 10000
```

### 11.4 监控告警建议

| 指标 | 阈值 | 行动 |
|------|------|------|
| Redis 内存使用率 | > 80% | 扩容内存或调整淘汰策略 |
| Redis 命中率 | < 90% | 检查缓存 TTL 是否过短 |
| 命令延迟 (P99) | > 10ms | 排查慢查询或网络问题 |
| 被限流的请求比例 | > 5% | 评估是否需要扩容服务 |
| Sentinel 故障转移次数 | > 0 | 排查 Redis 节点稳定性 |
| 熔断器被触发的实例 | > 0 | 检查对应 Provider 健康状况 |

### 11.5 关键代码文件索引

| 文件 | 行号范围 | 内容 |
|------|---------|------|
| `src/include/redisutil.h` | 全文件 | RedisClient 类声明 |
| `src/redisutil.cc` | 全文件 | RedisClient 类实现 |
| `src/include/redispool.h` | 全文件 | RedisConnectionPool 类声明 |
| `src/redispool.cc` | 全文件 | 连接池实现 |
| `src/include/redissentinel.h` | 全文件 | RedisSentinel 类声明 |
| `src/redissentinel.cc` | 全文件 | Sentinel 支持实现 |
| `src/include/ratelimiter.h` | 全文件 | RateLimiter 类声明 |
| `src/ratelimiter.cc` | 全文件 | 五种限流算法实现 |
| `src/include/distlock.h` | 全文件 | RedLock 类声明 |
| `src/distlock.cc` | 全文件 | 分布式锁 + Watchdog 实现 |
| `src/include/metricscollector.h` | 全文件 | MetricsCollector 类声明 |
| `src/metricscollector.cc` | 全文件 | 监控指标收集实现 |
| `src/mprpcchannel.cc` | 206-217 | 构造函数 Redis 初始化 |
| `src/mprpcchannel.cc` | 110-170 | discoverService Redis 缓存 |
| `src/mprpcchannel.cc` | 18-67 | CircuitBreaker Redis 持久化 |
| `src/mprpcchannel.cc` | 276-282 | Consumer 侧限流 |
| `src/mprpcprovider.cc` | 96-128 | Run() 中限流器初始化 |
| `src/mprpcprovider.cc` | 176-265 | OnMessage() 限流检查 |
| `src/CMakeLists.txt` | 全文件 | hiredis 链接配置 |
| `test.conf` | 全文件 | Redis 配置项 |
