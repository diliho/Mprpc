# MPRPC — 自研分布式 RPC 通信框架

基于 **C++11/14 + Muduo + Protobuf + ZooKeeper + Redis** 的轻量级分布式 RPC 中间件，提供高性能远程过程调用、服务注册与发现、分布式限流、熔断保护与实时监控能力。

---

## 核心特性

| 特性 | 实现方式 |
|------|---------|
| **RPC 通信** | Protobuf 自定义二进制协议（`RpcHeader` + 请求体）+ TCP 长连接 |
| **异步网络** | Muduo Reactor 模型，多线程 EventLoop（4 个 IO 线程） |
| **服务注册与发现** | ZooKeeper 临时节点 + 本地缓存（30s TTL），ZK 故障时使用本地缓存降级 |
| **故障熔断** | 三态熔断器（CLOSED/OPEN/HALF_OPEN），方法级粒度，阈值 5 次 / 恢复超时 30s |
| **分布式限流** | Redis 5 种算法（固定窗口 / 滑动日志 / 滑动计数器 / 令牌桶 / 漏桶），Lua 脚本保证原子性 |
| **实时监控** | Redis INCR/ZSet 按分钟聚合调用量、错误率、P99 延迟、限流拦截数 |
| **可观测性** | 结构化三级错误码（框架层 1xxx / 系统层 2xxx / 业务层 3xxx）+ 异步日志引擎 |
| **配置驱动** | INI 配置文件管理，支持 Consumer/Provider 独立限流开关 |

---

## 架构总览

```
┌────────────────────────────────────────────────────────────┐
│ Consumer (MprpcChannel)                                    │
│  CallMethod()                                              │
│    ├── 1. RateLimiter 限流检查（Consumer 侧）               │
│    ├── 2. CircuitBreaker 熔断检查（方法级）                  │
│    ├── 3. 服务发现：本地缓存(30s TTL) → ZooKeeper 回源       │
│    ├── 4. 选路：Round-Robin（当前为单实例）                  │
│    ├── 5. TCP 调用（最多重试 3 次，总超时 8s）              │
│    └── 6. MetricsCollector 记录监控指标                     │
└─────────────────────────┬──────────────────────────────────┘
                          │ RPC 请求
                          ▼
┌────────────────────────────────────────────────────────────┐
│ Provider (RpcProvider)                                     │
│  OnMessage()                                               │
│    ├── 1. RateLimiter 限流检查（Provider 侧，可选）          │
│    ├── 2. 反序列化 RpcHeader + 请求参数                      │
│    ├── 3. Protobuf 反射调用业务层 CallMethod()               │
│    ├── 4. 序列化响应，SendRpcResponse() 回写 TCP              │
│    └── 5. MetricsCollector 记录监控指标                     │
└─────────────────────────┬──────────────────────────────────┘
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
     ┌──────────┐   ┌──────────┐   ┌──────────┐
     │ZooKeeper │   │  Redis   │   │ Provider │
     │ 注册中心  │   │限流/监控  │   │   实例   │
     └──────────┘   └──────────┘   └──────────┘
```

---

## 项目结构

```
MPRPC/
├── autobuild.sh              # 一键构建脚本（安装依赖 → 编译 Muduo → 编译框架）
├── autodelete                # 清理脚本（删除构建产物、日志等）
├── CMakeLists.txt            # 根构建配置
├── test.conf                 # 框架配置文件
│
├── bin/                      # 编译产物
│   ├── provider              # RPC 服务端（UserService + FriendService）
│   ├── consumer              # RPC 客户端（UserService::Login）
│   ├── consumer_friend       # RPC 客户端（FriendService::GetFriendlist）
│   └── qps_test              # 多维度 QPS 压测工具（多并发等级、全链路延迟分布）
│
├── lib/                      # 静态库与头文件
│   ├── include/              # 公开头文件
│   └── libmprpc.a            # 静态库
│
├── docxs/                    # 设计文档
│   ├── redis中间件.md          # Redis 中间件设计（连接池、分布式锁、Redlock）
│   ├── 多实例部署设计.md        # 多实例部署设计（ZK 顺序节点、负载均衡）
│   ├── Zookeeper容错设计.md    # ZooKeeper 容错设计
│   ├── Zookeeper问题.md       # ZooKeeper 常见问题
│   └── 项目面试.md             # 项目面试问答
│
├── test/                     # 早期原型测试
│   └── protobuf/             # Protobuf 序列化测试（非 CMake 构建）
│
├── src/                      # 框架核心源码
│   ├── include/              # 内部头文件
│   │   ├── mprpcapplication.h    # 框架入口（单例 + Init 初始化）
│   │   ├── mprpcchannel.h        # RPC 通道（客户端，含 CircuitBreaker 内部类）
│   │   ├── mprpcprovider.h       # RPC 提供者（服务端，Muduo TcpServer 封装）
│   │   ├── mprpcconfig.h         # INI 配置解析
│   │   ├── mprpccontroller.h     # RPC 控制器（错误状态跟踪）
│   │   ├── rpcerror.h            # 三级错误码体系（Frame/System/Business）
│   │   ├── zookeeperutil.h       # ZooKeeper C API 封装（异步 init + 重连）
│   │   ├── redisutil.h           # Redis 客户端（String/Hash/List/Set/ZSet/Lua）
│   │   ├── ratelimiter.h         # 分布式限流器（5 种算法）
│   │   ├── metricscollector.h    # 监控指标收集器（Redis 聚合）
│   │   ├── logger.h              # 异步日志（按日期滚动）
│   │   └── lockqueue.h           # 线程安全阻塞队列（生产者-消费者）
│   │
│   ├── mprpcapplication.cc
│   ├── mprpcchannel.cc       # ~475 行（核心客户端逻辑）
│   ├── mprpcprovider.cc      # ~363 行（核心服务端逻辑）
│   ├── mprpcconfig.cc
│   ├── mprpccontroller.cc
│   ├── rpcerror.cc
│   ├── zookeeperutil.cc
│   ├── redisutil.cc          # ~553 行
│   ├── ratelimiter.cc        # ~213 行（5 种限流算法 Lua 脚本）
│   ├── metricscollector.cc
│   ├── Logger.cc
│   └── rpcheader.proto       # RPC 传输协议头定义
│
└── example/                  # 使用示例
    ├── user.proto            # UserServiceRpc（Login / Register）
    ├── friend.proto          # FriendServiceRpc（GetFriendlist）
    ├── callee/               # 服务提供者
    │   ├── main.cc
    │   ├── userservice.cc / userservice.h
    │   └── friendservice.cc / friendservice.h
    └── caller/               # 服务调用者
        ├── calluserservice.cc
        ├── callfriendservice.cc
        └── qps_test.cc       # 多线程 QPS 压测
```

---

## 快速开始

### 1. 安装依赖

```bash
sudo apt install cmake build-essential libboost-all-dev \
                 protobuf-compiler libprotobuf-dev \
                 libzookeeper-mt-dev libhiredis-dev
```

### 2. 编译

```bash
bash autobuild.sh
```

脚本自动完成：系统依赖检查 → 编译安装 Muduo（源码） → 生成 Protobuf → 编译框架静态库 → 编译示例 → 拷贝头文件到 `lib/include`

### 3. 配置

编辑 `test.conf`：

```conf
rpcserverip=127.0.0.1
rpcserverport=8000

zookeeperip=127.0.0.1
zookeeperport=2181

redisip=127.0.0.1
redisport=6379
redispassword=                    # Redis 密码（可选）

ratelimitalgorithm=3              # 0=FixedWindow 1=SlidingLog 2=SlidingCounter
                                  # 3=TokenBucket 4=LeakyBucket
ratelimitmaxqps=1000
ratelimitwindowsec=1
ratelimitenableprovider=false     # Provider 侧限流开关
ratelimitenableconsumer=false     # Consumer 侧限流开关

metricsenable=true                # 监控采集开关
```

### 4. 启动基础服务

```bash
# ZooKeeper（任选一种）
sudo systemctl start zookeeper
# 或: zkServer.sh start

# Redis
redis-server &
```

### 5. 启动 Provider

```bash
./bin/provider -i test.conf
```

预期输出：
```
RpcProvider start service at ip:127.0.0.1 port:8000
Redis connected to 127.0.0.1:6379
RateLimiter created
```

### 6. 运行 Consumer

新开终端：

```bash
./bin/consumer -i test.conf
# 输出: Login success

./bin/consumer_friend -i test.conf
# 输出: friend1 friend2 friend3

./bin/qps_test -i test.conf
```

---

## QPS 性能压测

`qps_test` 是多维度 RPC 性能压测工具，支持不同并发等级、多种 RPC 方法，输出完整延迟分布（P50/P95/P99）。

### 基本用法

```bash
./bin/qps_test -i test.conf
```

### 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-i <file>` | 必填 | 框架配置文件路径 |
| `-w <n>` | 50 | 预热请求数（每个线程独立预热，用于建立 TCP 连接和 ZK 缓存） |
| `-d <n>` | 5 | 每轮测试持续时间（秒） |
| `-c <n1,n2,...>` | 1,2,4,8,16 | 并发等级列表（逗号分隔），程序自动测试每个等级 |
| `-m <method>` | login | RPC 方法名，可选 `login` 或 `register` |
| `-q` | - | 安静模式，仅输出最终汇总 |

### 使用示例

```bash
# 默认压测：测试 login 方法，并发 1/2/4/8/16，每轮 5 秒
./bin/qps_test -i test.conf

# 测试 Register 方法，每轮 10 秒
./bin/qps_test -i test.conf -m register -d 10

# 自定义并发等级，预热 100 次
./bin/qps_test -i test.conf -c 1,4,16,32,64 -w 100

# 安静模式：只看最终结果汇总
./bin/qps_test -i test.conf -q
```




### 测试原理

1. 每个测试线程创建独立的 `MprpcChannel`（框架非线程安全，Channel 间隔离）
2. 预热阶段：发送 `-w` 次请求，建立 TCP 长连接 + 填充 ZK 地址缓存
3. 测试阶段：持续发送请求，记录每次调用的精确耗时（微秒级）
4. 每轮结束后，合并所有线程的延迟数据，排序后计算百分位值
5. 自动从低并发到高并发依次执行，观察系统吞吐量扩展曲线

---

## 核心设计

### RPC 调用流程

```
Consumer (MprpcChannel::CallMethod)           Provider (RpcProvider::OnMessage)
   │                                                      │
   ├─ 1. 限流检查 (Consumer 侧, 可选)                      │
   ├─ 2. 熔断器检查 (CLOSED 才放行)                         │
   ├─ 3. 服务发现: 本地缓存 → ZK 回源                       │
   ├─ 4. 选路 (Round-Robin)                               │
   ├─ 5. TCP 发送 ───────────────► OnMessage()
   │                                                      ├─ 1. 限流检查 (Provider 侧, 可选)
   │                                                      ├─ 2. 反序列化 RpcHeader + 参数
   │                                                      ├─ 3. 反射调用业务 CallMethod()
   │                                                      ├─ 4. 序列化响应
   │  ◄───────────── TCP 接收 ───────────────────────────└─ 5. SendRpcResponse()
   ├─ 6. 反序列化响应                                       │
   ├─ 7. 更新熔断器状态 (onSuccess / onFailure)             │
   └─ 8. 记录监控指标 (Redis INCR/ZSet)                    └─ 6. 记录监控指标 (Redis INCR/ZSet)
```

### 分布式限流

- **双端限流**：Consumer 侧（快速失败）和 Provider 侧（保护服务端），独立开关
- **5 种算法**：固定窗口 / 滑动窗口日志 / 滑动窗口计数器 / 令牌桶 / 漏桶
- **原子性保证**：所有算法通过 Redis Lua 脚本实现，Redis 故障时通过 fallthrough 保证可用性
- **键格式**：`mprpc:ratelimit:{service}:{method}:{client_ip}:{algorithm}`

### 熔断器

方法级三态熔断器，防止 Consumer 持续向故障 Provider 发送请求。嵌套定义在 `MprpcChannel` 中：

```
                 连续失败 ≥ 5 次
CLOSED ──────────────────────────────► OPEN
  ▲                                       │
  │                                       │ 30s 超时
  │                                       ▼
  └────────────── 1 次成功 ─────────── HALF_OPEN
                                      (探测请求)
```

### 服务发现

双层降级架构：

```
请求 → 本地内存缓存 (30s TTL) → 命中? → 返回
                              → 未命中 → ZooKeeper 查询 → 写入本地缓存
```

ZK 会话过期时自动 `reinitialize()` 重连，不阻塞正在进行的 RPC 调用。

### 传输协议

```
[4 字节 HeaderSize] [Protobuf RpcHeader] [Protobuf 请求参数]
                       ├─ service_name
                       ├─ method_name
                       └─ args_size
```

### 优雅关闭

Provider 收到 SIGINT 后：移除 ZK 临时注册节点 → 停止 Muduo EventLoop → 安全退出。

---

## 监控指标

通过 **Redis Hash** 按分钟聚合存储，后台线程异步批量写入，零阻塞 RPC 主流程。

### 键格式

```
mprpc:metrics:{Service}:{Method}:{YYYYMMDD-HHMM}    → 方法级统计
mprpc:metrics:{Service}:total:{YYYYMMDD-HHMM}       → 服务级汇总
```

### Hash 字段

| 字段 | 类型 | 含义 |
|------|------|------|
| calls | int64 | 总调用次数 |
| fails | int64 | 失败次数（业务异常） |
| timeouts | int64 | 超时次数（网络超时） |
| blocked | int64 | 限流拦截次数 |
| time_avg | int64 | 平均延迟 (ms) |
| time_max | int64 | 最大延迟 (ms) |
| time_min | int64 | 最小延迟 (ms) |
| time_p99 | int64 | P99 延迟 (ms) — 99% 请求低于该值 |
| time_p95 | int64 | P95 延迟 (ms) — 95% 请求低于该值 |

### 查看监控数据

```bash
# 查看所有监控 key
redis-cli KEYS "mprpc:metrics*"

# 查看某分钟的方法级统计
redis-cli HGETALL "mprpc:metrics:UserServiceRpc:Login:20260528-1420"

# 查看某分钟的服务级汇总
redis-cli HGETALL "mprpc:metrics:UserServiceRpc:total:20260528-1420"

# 计算错误率（需客户端计算）
redis-cli HGET "mprpc:metrics:UserServiceRpc:Login:20260528-1420" calls
redis-cli HGET "mprpc:metrics:UserServiceRpc:Login:20260528-1420" fails
# 错误率 = fails / calls * 100%
```

---

## 依赖清单

| 依赖 | 版本要求 | 用途 |
|------|---------|------|
| CMake | ≥ 3.0 | 构建系统 |
| Muduo | ≥ 2.0 | 异步网络库（Reactor 模型） |
| Protobuf | ≥ 3.0 | 序列化 + RPC 服务定义 |
| ZooKeeper (C API) | ≥ 3.4 | 服务注册与发现 |
| hiredis | ≥ 0.14 | Redis 客户端 |
| Boost | ≥ 1.50 | Muduo 依赖 |

---

## License

MIT 