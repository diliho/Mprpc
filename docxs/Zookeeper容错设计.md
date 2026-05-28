# MPRPC Zookeeper 容错机制设计文档

## 概述

本文档结合完整调用流程，描述 MPRPC 框架中围绕 Zookeeper 实现的容错机制。覆盖 **服务提供者启动 → ZK 注册 → 服务调用者发现 → RPC 通信 → 异常场景恢复** 全过程。

---

## 整体容错架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        容错架构总览                                  │
│                                                                     │
│  Provider 端                              Consumer 端               │
│  ┌──────────────────┐                    ┌──────────────────┐      │
│  │ 1. 优雅关闭       │                    │ 1. 熔断器         │      │
│  │    SIGINT 处理     │                    │    3 态状态机     │      │
│  │    ZK 节点清理     │                    │    CLOSED/OPEN    │      │
│  │                    │                    │    HALF_OPEN      │      │
│  ├──────────────────┤                    ├──────────────────┤      │
│  │ 2. ZK 会话过期    │                    │ 2. 地址缓存       │      │
│  │    自动重连        │                    │    内存缓存       │      │
│  │    重新注册服务    │                    │    ZK 不可用时降级  │      │
│  ├──────────────────┤                    ├──────────────────┤      │
│  │ 3. 错误响应回复   │                    │ 3. 重试+退避      │      │
│  │    OnMessage 异常 │ ◄──── ZK ────►     │    指数退避 1/2/4s│      │
│  │    返回 error 给  │                    │    随机抖动       │      │
│  │    客户端         │                    │                   │      │
│  └──────────────────┘                    ├──────────────────┤      │
│                                          │ 4. 超时控制       │      │
│                                          │    connect 3s     │      │
│                                          │    recv 5s        │      │
│                                          │    整体 8s        │      │
│                                          └──────────────────┘      │
│                                                                     │
│  ZKClient 公共层                                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ • sem_timedwait 5s 连接超时                                   │  │
│  │ • global_watcher 处理 ZOO_EXPIRED_SESSION_STATE → 自动重连    │  │
│  │ • doReinitialize() 销毁旧句柄 → 重新 Start → 触发重连回调     │  │
│  │ • Create 失败不再 exit，只打日志                                │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 完整调用流程中的容错

### 阶段一：Provider 启动 → ZK 注册

```
Provider 启动
    │
    ├── RpcProvider::Run()
    │     ├── 解析配置文件 (rpcserverip/rpcserverport, zookeeperip/zookeeperport)
    │     ├── 创建 TcpServer，绑定回调
    │     │
    │     ├── m_zkclient.Start()
    │     │     ├── zookeeper_init() 异步连接
    │     │     ├── 设置 global_watcher 回调 (传入 this 指针)
    │     │     ├── sem_timedwait(&m_sem, ts)  ← 容错①: 5s 超时
    │     │     │     ├── 连接成功 → ZOO_CONNECTED_STATE → sem_post → 继续
    │     │     │     └── 超时 → 返回 false, 打日志, 不退出
    │     │     │
    │     │     └── m_connected = true
    │     │
    │     ├── registerServicesToZK()
    │     │     ├── Create("/UserServiceRpc", nullptr, 0)          ← 持久节点
    │     │     ├── Create("/UserServiceRpc/Login", "ip:port", ZOO_EPHEMERAL)
    │     │     │     ├── CreateParentNodes() 递归创建父目录       ← 容错②: 父节点自动建
    │     │     │     ├── zoo_exists() 去重检查                     ← 容错③: 禁止重复创建
    │     │     │     └── 创建失败仅打日志，不再 exit              ← 容错④: 不崩溃
    │     │     └── ...
    │     │
    │     ├── 设置 ZK 重连回调                                     ← 容错⑤: 断线重注册
    │     │     m_zkclient.setOnReconnectCallback([this]() {
    │     │         registerServicesToZK();  // ZK 恢复后重新创建临时节点
    │     │     });
    │     │
    │     ├── signal(SIGINT, handleSigInt)                          ← 容错⑥: 优雅关闭
    │     │
    │     ├── server.start()
    │     └── m_eventloop.loop()
    │
    └── Provider 就绪，等待 RPC 请求
```

### 阶段二：Consumer 启动 → 服务发现

```
Consumer 启动
    │
    ├── MprpcChannel 构造函数
    │     ├── m_zkclient.Start()
    │     │     ├── 成功 → m_zk_available = true
    │     │     └── 失败 → m_zk_available = false
    │     │              Consumer 继续运行，后续降级到缓存地址      ← 容错⑦: ZK 降级
    │     └── LOG_INFO / LOG_ERROR
    │
    └── 等待 CallMethod 调用
```

### 阶段三：Consumer CallMethod → 服务发现

```
stub.Login(&controller, &request, &response, nullptr)
    │
    ├── MprpcChannel::CallMethod()
    │     │
    │     ├── ① 检查熔断器                                          ← 容错⑧: 熔断
    │     │     method_path = "/UserServiceRpc/Login"
    │     │     cb.allowRequest()
    │     │     ├── CLOSED   → 正常放行
    │     │     ├── OPEN     → 检查是否到半开时间(30s)
    │     │     │     ├── 未到 → 快速失败，返回 "circuit breaker open"
    │     │     │     └── 已到 → HALF_OPEN → 放行一个探活请求
    │     │     └── HALF_OPEN → 放行
    │     │
    │     ├── ② 本地缓存查询                                        ← 容错⑨: 缓存
    │     │     getCachedAddress(method_path)
    │     │     ├── 命中且 ZK 不可用 → 直接返回缓存地址
    │     │     └── 未命中 → 进入 ZK 查询
    │     │
    │     ├── ③ ZK 服务发现（带重试）                               ← 容错⑩: 重试退避
    │     │     discoverService(method_path)
    │     │     for (i = 0; i < 3; i++) {
    │     │         host_data = m_zkclient.GetData(method_path)
    │     │         if (成功) → setCachedAddress → 返回
    │     │         else → sleep(1000 << i + rand(500))  // 1s, 2s, 4s
    │     │     }
    │     │     └── 全部失败 → 降级到缓存                            ← 容错⑪: 缓存降级
    │     │           ├── 有缓存 → 使用旧地址（打 WARN 日志）
    │     │           └── 无缓存 → 熔断器记录失败 → 返回错误
    │     │
    │     ├── ④ 解析 IP:Port
    │     │     host_data.find(":")  →  ip + port
    │     │     └── 格式错误 → controller.SetFailed → 返回
    │     │
    │     ├── ⑤ 检查整体超时                                        ← 容错⑫: 超时控制
    │     │     elapsed > 8s → controller.SetFailed("RPC call timeout")
    │     │                     熔断器记录失败 → 返回
    │     │
    │     ├── ⑥ TCP 连接（带超时）                                  ← 容错⑬: connect 超时
    │     │     socket → connectWithTimeout(fd, addr, 3)
    │     │     ├── 成功 → 继续
    │     │     └── 失败(3s 超时) → close → 熔断器记录失败 → 返回
    │     │
    │     ├── ⑦ 发送请求
    │     │     send() 失败 → close → 返回错误
    │     │
    │     ├── ⑧ 接收响应（带超时）                                  ← 容错⑭: recv 超时
    │     │     SO_RCVTIMEO = 5s
    │     │     recv() 结果:
    │     │     ├── recv_size > 0 → 解析响应
    │     │     ├── recv_size == 0 → "connection closed by server"
    │     │     ├── EAGAIN/EWOULDBLOCK → "RPC recv timeout (5s)"
    │     │     └── 其他错误 → MESSAGE_RECV_FAILED
    │     │     失败时 → 熔断器记录失败
    │     │
    │     ├── ⑨ 解析响应
    │     │     ├── response_args_size == 0 → "server error"  ← Provider 返回的错误
    │     │     ├── ParseFromString 失败 → DESERIALIZE_FAILED
    │     │     └── 成功 → 熔断器记录成功 → m_zk_available = true
    │     │
    │     └── close(fd) → 返回
```

### 阶段四：Provider OnMessage → 业务处理 → 响应

```
muduo 接收 TCP 数据
    │
    ├── RpcProvider::OnMessage()
    │     │
    │     ├── 读取 header_size (4B)
    │     │     └── 数据不足4B → sendErrorResponse()                 ← 容错⑮: 回复错误
    │     │
    │     ├── 反序列化 RpcHeader
    │     │     └── 失败 → sendErrorResponse(conn, "parse rpc header error")
    │     │
    │     ├── 检查 args_size 合法性
    │     │     └── 越界 → sendErrorResponse(conn, "invalid rpc args size")
    │     │
    │     ├── 查找 service_name 和 method_name
    │     │     ├── 服务找不到 → sendErrorResponse("service not found")
    │     │     └── 方法找不到 → sendErrorResponse("method not found")
    │     │
    │     ├── 反序列化请求参数
    │     │     └── 失败 → sendErrorResponse("request parse error")
    │     │
    │     ├── service->CallMethod()  →  业务层 Login()
    │     │
    │     └── done->Run()  →  SendRpcResponse()
    │           └── 序列化 response → conn->send()
    │
    └── conn->shutdown()
```

---

## 异常场景与容错对照

### 场景 1：ZK 服务器宕机

```
时间轴：
│
├── [Provider 端]
│     ├── ZK 宕机 → TCP 连接断开
│     ├── ZK C API 检测到连接断开
│     ├── global_watcher 收到 ZOO_CONNECTING_STATE
│     │     └→ LOG_INFO("ZK connecting...")
│     ├── ZK C API 自动尝试重连
│     │
│     ├── [情况 A：ZK 快速恢复（10s 内）]
│     │     └→ ZOO_CONNECTED_STATE → 会话未过期 → 正常恢复
│     │
│     └── [情况 B：ZK 宕机超过会话超时（3s）]
│           ├── ZOO_EXPIRED_SESSION_STATE 触发
│           ├── client->doReinitialize()
│           │     ├── zookeeper_close() 旧句柄
│           │     ├── ZKClient::Start() 重连
│           │     └── 成功 → 触发 onReconnectCallback
│           └── callback → registerServicesToZK()
│                 └── 重新创建所有临时节点 ← 服务恢复！
│
├── [Consumer 端]
│     ├── discoverService() 调用 m_zkclient.GetData()
│     ├── zoo_get 失败 → 返回空字符串
│     ├── 重试 3 次（1s/2s/4s 退避）
│     │     ├── [ZK 恢复] → 获取地址成功 → 更新缓存 → 正常调用
│     │     └── [ZK 未恢复] → 降级到本地缓存地址
│     │           ├── 有缓存 → 直连 Provider（WARN 日志）
│     │           └── 无缓存 → 熔断器 onFailure → 返回错误
│     │
│     └── 后续调用：
│           ├── m_zk_available = false → 直接读缓存，每隔 100 次尝试一次 ZK
│           └── ZK 恢复后 → m_zk_available = true → 恢复 ZK 查询
│
└── [恢复]
      ├── Consumer 的某次探活成功后 → m_zk_available = true
      └── Provider 的临时节点已重新注册 → 链路完全恢复
```

### 场景 2：Provider 进程崩溃

```
时间轴：
│
├── Provider Ctrl+C 或崩溃
│     ├── [正常关闭 SIGINT]
│     │     ├── handleSigInt() 触发
│     │     ├── 遍历 m_serviceMap，删除 ZK 临时节点
│     │     │     zoo_delete("/UserServiceRpc/Login")
│     │     │     zoo_delete("/UserServiceRpc/Register")
│     │     ├── m_eventloop.quit()
│     │     └── 进程退出
│     │
│     └── [异常崩溃]
│           ├── OS 关闭 TCP 连接
│           ├── ZK 检测到会话断开
│           ├── 等待 session_timeout (3s)
│           └── ZK 自动删除所有临时节点
│
├── [Consumer 端]
│     ├── 下一次调用时：
│     │     ├── discoverService 获取到缓存地址（ZK 上节点已删除但缓存还在）
│     │     ├── connectWithTimeout 连接 Provider → 失败（3s 超时）
│     │     ├── 熔断器 onFailure → 累计失败计数
│     │     │
│     │     └── 继续下一次 discoverService：
│     │           ├── ZK GetData 返回空（节点已删除）
│     │           ├── 从缓存读取 → 还是旧地址 → 连接再次失败
│     │           └── 熔断器累计 5 次失败 → OPEN 状态
│     │                 └─ 此后 30s 内快速失败，不再进行无效连接
│     │
│     └── [Provider 重启后]
│           ├── Provider 重新注册临时节点到 ZK
│           ├── Consumer 的 discoverService 获取到新地址
│           ├── 熔断器检测到成功 → CLOSED 状态
│           └── 恢复正常调用
│
└── [恢复]
      ├── 熔断器 CLOSED
      ├── 缓存地址已更新
      └── Provider 重新服务
```

### 场景 3：网络抖动导致 Consumer 超时

```
时间轴：
│
├── [正常调用]
│     ├── discoverService → 获取地址
│     ├── socket → connect (3s 超时) → 成功
│     │
│     ├── send 请求
│     │
│     ├── [网络延迟]
│     │     └── recv 等待中...
│     │           ├── 5s 内收到响应 → 正常返回
│     │           └── 超过 5s → SO_RCVTIMEO 触发
│     │                 └── EAGAIN / EWOULDBLOCK
│     │                       ├── controller->SetFailed("RPC recv timeout (5s)")
│     │                       ├── 熔断器 onFailure → 记录失败
│     │                       └── close(fd) → 返回
│     │
│     └── [重试]
│           ├── 业务层可以选择重试（幂等方法）
│           └── discoverService 重新获取地址并连接
│
└── [恢复]
      └── 网络恢复后，下一次调用正常
```

### 场景 4：Provider 处理请求时出错

```
时间轴：
│
├── [Provider 端]
│     ├── OnMessage 收到请求
│     ├── 反序列化 RpcHeader → 失败
│     ├── sendErrorResponse(conn, "parse rpc header error")   ← 容错: 回复错误
│     │     ├── 构造空 args 的响应
│     │     ├── conn->send(send_str)  ← 发送错误响应
│     │     └── conn->shutdown()
│     │
│     └── [其他错误场景同样回复]
│           ├── "invalid rpc header size"
│           ├── "service not found"
│           ├── "method not found"
│           └── "request parse error"
│
├── [Consumer 端]
│     ├── recv 收到数据（不会阻塞！）
│     ├── 解析 response_header
│     ├── response_args_size == 0
│     │     └── controller->SetFailed("server error")
│     │
│     └── 不会永久阻塞，业务层可以及时感知错误
│
└── [未加容错前的问题]
      └── Provider 直接 return → Consumer 的 recv() 永远阻塞
            → 直到 TCP  keepalive 超时（数分钟）
```

---

## 代码实现要点

### 1. ZKClient::Start() — 连接超时

```cpp
bool ZKClient::Start()
{
    // ...
    m_zhandle = zookeeper_init(conststr.c_str(), global_watcher, 3000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle)
    {
        LOG_ERROR("zookeeper_init error");
        return false;        // 不再 exit，调用方决定是否重试
    }

    zoo_set_context(m_zhandle, this);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;                                    // 5 秒超时
    int ret = sem_timedwait(&m_sem, &ts);
    if (ret == -1)
    {
        LOG_ERROR("ZK connect timeout (5s)");
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        return false;
    }

    m_connected = true;
    return true;
}
```

### 2. global_watcher — 会话过期处理

```cpp
void global_watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx)
{
    if (type != ZOO_SESSION_EVENT) return;
    ZKClient* client = static_cast<ZKClient*>(watcherCtx);
    if (!client) return;

    if (state == ZOO_CONNECTED_STATE)
    {
        sem_post(client->getConnectSem());
    }
    else if (state == ZOO_EXPIRED_SESSION_STATE)
    {
        LOG_ERROR("ZK session expired, reinitializing...");
        client->doReinitialize();  // 自动重连 + 触发重新注册
    }
    else if (state == ZOO_AUTH_FAILED_STATE)
    {
        LOG_ERROR("ZK auth failed");
    }
}
```

### 3. doReinitialize() — 重连与恢复

```cpp
void ZKClient::doReinitialize()
{
    if (m_zhandle)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    m_connected = false;
    sem_destroy(&m_sem);
    sem_init(&m_sem, 0, 0);

    if (Start())    // 重新连接
    {
        if (m_on_reconnect_cb)
        {
            m_on_reconnect_cb();   // → registerServicesToZK()
        }
    }
    else
    {
        LOG_ERROR("ZK reconnection failed, will retry later");
        // 下次会话事件会再次触发 reinitialize
    }
}
```

### 4. 熔断器 — 防止雪崩

```cpp
bool CircuitBreaker::allowRequest()
{
    switch (m_state)
    {
    case CLOSED:
        return true;
    case OPEN:
        // 30 秒后尝试探活
        if (elapsed >= m_timeout_sec) {
            m_state = HALF_OPEN;
            return true;
        }
        return false;
    case HALF_OPEN:
        return true;  // 探活请求
    }
}
void CircuitBreaker::onFailure()
{
    if (++m_failure_count >= m_threshold) {
        m_state = OPEN;  // 5 次连续失败 → 熔断
    }
}
void CircuitBreaker::onSuccess()
{
    m_failure_count = 0;
    if (m_state == HALF_OPEN) m_state = CLOSED;  // 恢复
}
```

### 5. 服务发现重试+退避

```cpp
std::string MprpcChannel::discoverService(const std::string& method_path)
{
    // 1. ZK 不可用时读缓存
    if (!m_zk_available) {
        std::string cached = getCachedAddress(method_path);
        if (!cached.empty()) return cached;
    }

    // 2. ZK 查询（最多 3 次，指数退避）
    for (int i = 0; i < MAX_RETRY; ++i)
    {
        std::string host_data = m_zkclient.GetData(method_path.c_str());
        if (!host_data.empty()) {
            setCachedAddress(method_path, host_data);
            return host_data;
        }
        if (i < MAX_RETRY - 1) {
            usleep(((1000 << i) + rand() % 500) * 1000);  // 1s/2s/4s + 抖动
        }
    }

    // 3. 降级到缓存
    return getCachedAddress(method_path);
}
```

---

## 容错机制全景对照表

| 编号 | 容错措施 | 触发条件 | 行为 | 受益方 |
|------|---------|---------|------|--------|
| ① | `sem_timedwait` 5s 超时 | ZK 连接 5s 内未就绪 | 返回 false，不阻塞 | Provider/Consumer |
| ② | 递归创建父节点 | Znode 父路径不存在 | 自动创建所有缺失父节点 | Provider 注册 |
| ③ | 节点去重 | Znode 已存在 | `zoo_exists` 检查后跳过 | Provider 注册 |
| ④ | Create 失败不退出 | ZK 创建节点失败 | 仅打 ERROR 日志，继续运行 | Provider |
| ⑤ | ZK 重连回调 | `ZOO_EXPIRED_SESSION_STATE` | 自动重新注册所有临时节点 | Provider 服务 |
| ⑥ | SIGINT 优雅关闭 | `Ctrl+C` | 删除 ZK 临时节点 → quit loop | Consumer 发现 |
| ⑦ | ZK 不可用时降级 | `MprpcChannel` 构造时 ZK 连不上 | `m_zk_available=false`，后续读缓存 | Consumer |
| ⑧ | 熔断器（三态） | 连续 5 次调用失败 | OPEN 30s → HALF_OPEN 探活 → CLOSED 恢复 | Consumer |
| ⑨ | 地址缓存 | 每次成功服务发现后 | 缓存 `method_path → ip:port` | Consumer |
| ⑩ | 重试+指数退避 | ZK GetData 失败 | 3 次重试，1s/2s/4s+随机抖动 | Consumer |
| ⑪ | 缓存降级 | ZK 全部重试失败 | 使用缓存中的旧地址（发出告警） | Consumer |
| ⑫ | 整体超时 8s | `CallMethod` 耗时超 8s | 快速失败，释放线程 | Consumer |
| ⑬ | connect 超时 3s | TCP 连接 3s 未建立 | 失败 → 熔断器记录 | Consumer |
| ⑭ | recv 超时 5s | 5s 内未收到响应 | `SO_RCVTIMEO` 触发 → 超时错误 | Consumer |
| ⑮ | Provider 回复错误 | 请求解析/服务/方法找不到 | `sendErrorResponse()` 避免客户端阻塞 | Consumer |

---

## 设计原则总结

1. **不单点依赖 ZK**：通过缓存+降级，ZK 短暂不可用时系统仍可用。
2. **快速失败 vs. 熔断**：瞬态故障通过重试容忍，持续故障通过熔断器快速失败。
3. **进程不挂起**：所有阻塞操作（ZK 连接、TCP 通信）都有超时保护。
4. **自动恢复**：ZK 会话过期后自动重连+重新注册，无需人工介入。
5. **优雅关闭**：进程退出时主动清理 ZK 节点，避免 Consumer 无效重试。
6. **错误不沉默**：Provider 端出错时回复客户端，避免 Consumer 永久阻塞。
