# Zookeeper 在 MPRPC 项目中的作用、配置与实现原理

## Q1: Zookeeper 在 MPRPC 项目中扮演什么角色？

**A:** Zookeeper 在 MPRPC 项目中充当 **分布式服务注册与发现中心** 的角色。具体来说：

- **服务注册**：RPC 服务端（Provider）启动时，将自己提供的服务名称、方法名称以及所在的主机 IP 和端口号注册到 Zookeeper 的指定节点上。
- **服务发现**：RPC 客户端（Consumer）在发起远程调用时，不再硬编码服务端地址，而是根据服务名和方法名从 Zookeeper 查询获取对应的服务端 IP 和端口，从而动态地建立连接。
- **健康检测**：Provider 注册的节点是 **临时节点（Ephemeral Node）**，当 Provider 崩溃或断开与 Zookeeper 的会话时，Znode 自动删除，Consumer 便不会发现不可用的服务节点。

简单来说，Zookeeper 解除了服务端与客户端之间的地址硬耦合，使得服务端可以动态扩缩容，客户端始终能够发现可用的服务节点。

---

## Q2: 项目中 Zookeeper 是如何配置的？

**A:** Zookeeper 的配置通过一个文本配置文件（如项目根目录的 `test.conf`）实现，框架启动时加载解析。配置项如下：

```conf
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

| 配置项 | 含义 | 默认值 |
|--------|------|--------|
| `rpcserverip` | RPC 服务端监听的 IP 地址 | 127.0.0.1 |
| `rpcserverport` | RPC 服务端监听的端口 | 8000 |
| `zookeeperip` | Zookeeper 服务的 IP 地址 | 127.0.0.1 |
| `zookeeperport` | Zookeeper 服务的端口 | 2181 |

**配置加载流程**:

1. 程序通过命令行参数 `-i` 指定配置文件路径（如 `./bin/provider -i test.conf`）。
2. `MprpcApplication::Init()` 解析命令行参数，调用 `MprpcConfig::LoadConfigfile()` 读取配置文件。
3. `MprpcConfig` 逐行解析配置文件：跳过空行和 `#` 注释，按 `key=value` 格式提取配置项，存入 `unordered_map`。
4. 程序中通过 `MprpcApplication::GetConfig().Load("zookeeperip")` 获取 Zookeeper 地址。

**Zookeeper 客户端初始化**（在 `ZKClient::Start()` 中）：

```cpp
std::string host = MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
std::string port = MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
std::string conststr = host + ":" + port;
m_zhandle = zookeeper_init(conststr.c_str(), global_watcher, 3000, nullptr, nullptr, 0);
```

- 连接超时设为 3000ms
- 使用 `global_watcher` 作为会话事件的回调函数
- 通过信号量 `sem_wait` 同步等待连接建立完成

---

## Q3: Zookeeper 如何实现分布式服务注册与发现？

**A:** MPRPC 框架基于 Zookeeper 的 **树形命名空间**、**临时节点** 和 **Watcher 机制**，实现了完整的服务注册与发现流程。下面分角色详细说明。

### 一、服务注册（Provider 端）

Provider（服务提供者）启动时的注册流程在 `RpcProvider::Run()` 中完成：

```
Zookeeper 节点结构（示例）:
/
├── UserServiceRpc                  # 持久节点（服务名）
│   └── Login                       # 临时节点（方法名，数据为 IP:Port）
│   └── Register                    # 临时节点（方法名，数据为 IP:Port）
└── FriendServiceRpc                # 持久节点（服务名）
    └── GetFriendList               # 临时节点（方法名，数据为 IP:Port）
```

**注册步骤**：

1. **建立 Zookeeper 连接**
   ```cpp
   m_zkclient.Start();  // 连接 ZK，等待会话就绪
   ```

2. **遍历已注册的服务和方法**，为每个方法创建 Znode 节点
   ```cpp
   for (auto &sp : m_serviceMap) {
       // 创建服务级别的持久节点: /UserServiceRpc
       std::string service_path = "/" + sp.first;
       m_zkclient.Create(service_path.c_str(), nullptr, 0);
       
       for (auto &mp : sp.second.m_methodMap) {
           // 创建方法级别的临时节点: /UserServiceRpc/Login
           // 节点数据为当前服务的 IP:Port
           std::string method_path = service_path + "/" + mp.first;
           char method_path_data[128] = {0};
           sprintf(method_path_data, "%s:%d", ip.c_str(), port);
           m_zkclient.Create(method_path.c_str(), method_path_data, 
                             strlen(method_path_data), ZOO_EPHEMERAL);
       }
   }
   ```

**关键设计**：

| 特性 | 实现方式 | 作用 |
|------|---------|------|
| **持久节点** | 服务名节点（如 `/UserServiceRpc`）用 `state=0` 创建 | 作为命名空间的目录节点，不随会话消失 |
| **临时节点** | 方法名节点用 `ZOO_EPHEMERAL` 标志创建 | Provider 崩溃或断连时节点自动删除，防止客户端调用已宕机的服务 |
| **递归建父路径** | `CreateParentNodes()` 逐级检查并创建父节点 | 支持多级命名空间，避免因父节点不存在导致创建失败 |
| **节点数据** | 存储 `IP:Port` 字符串 | Consumer 通过此数据获取服务端实际地址 |

### 二、服务发现（Consumer 端）

Consumer（服务调用者）在 `MprpcChannel::CallMethod()` 中完成服务发现：

```cpp
// 1. 构建 ZK 路径: /UserServiceRpc/Login
std::string method_path = "/" + service_name + "/" + method_name;

// 2. 从 Zookeeper 获取该路径下的数据（即 IP:Port）
std::string host_data = m_zkclient.GetData(method_path.c_str());

// 3. 解析 IP 和端口
int idx = host_data.find(":");
std::string ip = host_data.substr(0, idx);
uint16_t port = atoi(host_data.substr(idx + 1).c_str());

// 4. 连接目标服务端
struct sockaddr_in server_addr;
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(port);
server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
```

**发现流程**：

```
Consumer 调用方法 Login
    │
    ├─→ 构造 ZK 路径 "/UserServiceRpc/Login"
    ├─→ ZKClient::GetData("/UserServiceRpc/Login")
    ├─→ Zookeeper 返回 "127.0.0.1:8000"
    ├─→ 解析出 ip=127.0.0.1, port=8000
    ├─→ socket 连接到 127.0.0.1:8000
    ├─→ 发送序列化的 RPC 请求
    └─→ 接收 RPC 响应
```

### 三、Zookeeper 客户端封装（ZKClient）

`ZKClient` 类对 Zookeeper C API 做了封装：

| 方法 | 功能 | 关键实现 |
|------|------|---------|
| `Start()` | 初始化 ZK 连接 | `zookeeper_init()` 异步创建句柄，信号量同步等待 `ZOO_CONNECTED_STATE` 事件 |
| `Create()` | 创建 Znode 节点 | 先递归创建父节点，再检查节点是否已存在，不存在则创建（支持临时/持久） |
| `GetData()` | 获取节点数据 | 调用 `zoo_get()` 读取节点存储的服务地址 |
| `CreateParentNodes()` | 递归创建父路径 | 从叶子向根逐级检查，缺失则创建 |

**同步连接机制**（`Start()`）：

```cpp
void ZKClient::Start() {
    // 异步连接，注册 global_watcher 回调
    m_zhandle = zookeeper_init(conststr.c_str(), global_watcher, 3000, nullptr, nullptr, 0);
    zoo_set_context(m_zhandle, &m_sem);
    sem_wait(&m_sem);  // 等待连接就绪
}

// Watcher 回调 — 在独立的 ZK 线程中执行
void global_watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
        sem_t* sem = (sem_t*)zoo_get_context(zh);
        sem_post(sem);  // 唤醒 Start() 中等待的线程
    }
}
```

---

## Q4: 为什么要同时使用持久节点和临时节点？

**A:** 这是为了兼顾 **目录结构的稳定性** 和 **服务可用性的动态检测**。

| 节点类型 | 用途 | 生命周期 | 示例 |
|---------|------|---------|------|
| **持久节点**（服务名） | 作为命名空间的目录节点 | 手动删除前一直存在 | `/UserServiceRpc` |
| **临时节点**（方法名） | 存储具体的服务地址 | 随 Provider 会话结束自动删除 | `/UserServiceRpc/Login` → `127.0.0.1:8000` |

**为什么不全部使用持久节点？**
- 如果服务名节点也是临时的，那么 Provider 重启后所有节点路径都需要重新创建。
- 持久节点作为"目录"存在后，临时子节点的创建更快（无需递归建父路径）。

**为什么不全部使用临时节点？**
- 如果方法节点也是持久的，Provider 崩溃后该节点依然存在，Consumer 仍会尝试连接已宕机的服务端，导致调用失败。
- 临时节点保证：Provider 下线 → ZK 会话断开 → Znode 自动删除 → Consumer 无法发现不可用节点。

---

## Q5: 如果 Zookeeper 不可用会发生什么？

**A:** 根据代码中的错误处理逻辑：

1. **Provider 端**：`zookeeper_init()` 返回 nullptr 时，打印错误日志并调用 `exit(EXIT_FAILURE)` 退出进程。
2. **Consumer 端**：`MprpcChannel` 构造函数中调用 `m_zkclient.Start()`，如果 ZK 不可用同样会退出。`GetData()` 返回空字符串时会通过 `MprpcController::SetFailed()` 设置错误状态，调用方可以通过 `controller->Failed()` 和 `controller->ErrorText()` 获取错误信息。

实际生产部署时，Zookeeper 通常以集群模式（3 或 5 节点）运行，保证高可用。

---

## Q6: Zookeeper 相比其他服务注册方案（如 etcd、Consul、Nacos）有何特点？

| 特性 | Zookeeper | etcd | Consul | Nacos |
|------|-----------|------|--------|-------|
| CAP 模型 | CP（一致性+分区容错） | CP | CP | CP/AP 可切换 |
| 一致性协议 | ZAB（Zookeeper Atomic Broadcast） | Raft | Raft | Raft 自研 |
| 语言 | Java | Go | Go | Java |
| 临时节点 | ✅ 支持 | ✅ 支持（Lease） | ✅ 支持（Health Check） | ✅ 支持 |
| Watcher 机制 | ✅ 原生 | ✅ Watch 机制 | ✅ Health Check | ✅ 推送 |
| 主要场景 | 分布式协调、锁、配置、服务发现 | 配置中心、服务发现 | 服务发现、配置、健康检查 | 服务发现、配置、动态 DNS |

MPRPC 选择 Zookeeper 的主要原因是其 **成熟稳定的临时节点 + Watcher 机制**，非常适合 RPC 框架的服务注册与发现场景。

---

## Q7: 为什么不直接在 Consumer 中硬编码 Provider 地址？

**A:** 硬编码地址在分布式系统中有以下问题：

| 问题 | 说明 |
|------|------|
| **单点故障** | 固定的 Provider 崩溃后，Consumer 无法切换到其他副本 |
| **无法扩缩容** | 增加 Provider 实例时需要修改所有 Consumer 配置 |
| **环境耦合** | 开发/测试/生产环境的地址不同，部署时需要修改配置 |
| **运维成本高** | 每次地址变更都需要重启 Consumer 进程 |

通过 Zookeeper 实现服务注册与发现后：

- Provider 启动时自动注册到 ZK，下线时自动摘除
- Consumer 每次调用前从 ZK 获取最新的可用地址
- 支持多 Provider 实例的水平扩展（可通过 ZK 的节点列表实现负载均衡）
- 环境解耦，只需配置 ZK 地址即可

---

## Q8: 项目的 Znode 节点数据结构设计有何考虑？

**A:** 当前设计将 **每个方法作为一个 Znode 节点**，节点数据为该方法的提供者地址：

```
/服务名/方法名  →  "IP:Port"
```

**为什么以方法名为粒度而不是服务名？**

- 更细粒度的控制：同一个服务的不同方法可以分布在不同机器上。
- 更灵活的负载均衡：可以针对热点方法单独扩容。

**节点数据的格式** 采用简单 `IP:Port` 字符串（如 `127.0.0.1:8000`），解析逻辑在 `mprpcchannel.cc:132-143` 中实现，直接基于字符串查找 `:` 分隔符，轻量高效。

---

## Q9: ZKClient 的实现中有哪些值得注意的细节？

**A:** 以下是几个关键实现细节：

### 1. 信号量同步等待连接
```
Zookeeper C API 的 zookeeper_init() 是异步的
        │
        ▼
ZKClient::Start() 调用 sem_wait() 阻塞
        │
        ▼
Zookeeper 连接成功后回调 global_watcher()
        │
        ▼
watcher 中 sem_post() 唤醒等待线程
        │
        ▼
Start() 返回，连接已就绪
```

### 2. 递归创建父节点
`CreateParentNodes()` 方法确保在创建多级路径（如 `/a/b/c`）时，父节点 `/a`、`/a/b` 不存在时会逐级创建，避免 `zoo_create()` 因父节点缺失而失败。

### 3. 节点去重
创建节点前先调用 `zoo_exists()` 检查节点是否存在，避免重复创建导致报错。

### 4. 资源管理
析构函数 `~ZKClient()` 中调用 `zookeeper_close(m_zhandle)` 释放句柄，调用 `sem_destroy(&m_sem)` 销毁信号量。

---

## Q10: 如果要支持多 Provider 实例（集群部署），当前框架需要做什么扩展？

**A:** 当前框架的 Znode 设计（`/服务名/方法名 → IP:Port`）是 **一对一** 的。要支持多实例，可以做以下扩展：

### 方案一：方法节点存储多地址

将节点数据格式改为 `IP1:Port1,IP2:Port2`，Consumer 从中选取一个进行连接（如随机或轮询）。

### 方案二：方法节点下挂子节点

```
/UserServiceRpc/Login/instance-1  →  192.168.1.1:8000
                  /instance-2  →  192.168.1.2:8000
```

Consumer 获取所有子节点列表，按负载均衡策略选择一个连接。

### 方案三：集成 ZK Watcher

在 Consumer 端注册 Watcher 监听方法节点的变化：
```cpp
// 伪代码示例
zoo_wget_children(m_zhandle, "/UserServiceRpc/Login", 
                  watcher_callback, context, &children, &children_count);
```

当 Provider 增加或减少时，Watcher 回调自动通知 Consumer 更新本地缓存的服务地址列表。

---

## Q11: 代码层面怎么使用这个框架？怎么定义 RPC 服务？

**A:** 使用 MPRPC 框架开发一个 RPC 服务分三步：**定义 proto 服务** → **实现服务端** → **实现客户端**。下面通过示例逐步说明。

---

### 第一步：编写 `.proto` 文件定义服务接口

以 `example/user.proto` 为例：

```protobuf
syntax = "proto3";
package fixbug;                      // 包名，对应 C++ 的命名空间
option cc_generic_services = true;   // 必须开启，告诉 protoc 生成 RPC 服务代码

// 1. 定义错误码消息
message ResultCode {
    int32 errorcode = 1;
    bytes errormsg  = 2;
}

// 2. 定义请求消息
message LoginRequest {
    bytes name = 1;
    bytes pwd  = 2;
}

message RegisterRequest {
    uint32 id   = 1;
    bytes  name = 2;
    bytes  pwd  = 3;
}

// 3. 定义响应消息
message LoginResponse {
    ResultCode result = 1;
    bool success      = 2;
}

message RegisterResponse {
    ResultCode result = 1;
    bool success      = 2;
}

// 4. 定义 RPC 服务（核心）
service UserServiceRpc {
    rpc Login(LoginRequest) returns (LoginResponse);
    rpc Register(RegisterRequest) returns (RegisterResponse);
}
```

**关键约束**：
- 必须添加 `option cc_generic_services = true;`，否则 protoc 不会生成 `UserServiceRpc` 和 `UserServiceRpc_Stub` 类。
- 每个 RPC 方法必须定义独立的 Request 和 Response message。

**编译 proto 文件**：

```bash
protoc --proto_path=. --cpp_out=. user.proto
```

生成 `user.pb.h` 和 `user.pb.cc`，其中 `UserServiceRpc` 是服务基类（服务端继承），`UserServiceRpc_Stub` 是客户端存根类。

---

### 第二步：实现 RPC 服务端（Provider）

#### 2.1 编写业务逻辑

创建 `userservice.h`：

```cpp
#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <string>
#include "user.pb.h"
#include "mprpcprovider.h"

class UserService : public fixbug::UserServiceRpc   // 继承 protoc 生成的基类
{
public:
    // 本地业务方法（纯业务逻辑，不依赖框架）
    bool Login_local(std::string name, std::string pwd);
    bool Register_local(std::uint32_t id, std::string name, std::string pwd);

    // 重写 RPC 接口方法（框架回调入口）
    void Login(::google::protobuf::RpcController *controller,
               const ::fixbug::LoginRequest *request,
               ::fixbug::LoginResponse *response,
               ::google::protobuf::Closure *done) override;

    void Register(::google::protobuf::RpcController* controller,
                  const ::fixbug::RegisterRequest* request,
                  ::fixbug::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override;
};

#endif
```

创建 `userservice.cc`，实现业务和 RPC 方法：

```cpp
#include "userservice.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "logger.h"

// 本地业务逻辑
bool UserService::Login_local(std::string name, std::string pwd)
{
    LOG_INFO("doing login service: name=%s, pwd=%s", name.c_str(), pwd.c_str());
    return name != "invalid" && pwd != "invalid";
}

bool UserService::Register_local(std::uint32_t id, std::string name, std::string pwd)
{
    LOG_INFO("doing register service: id=%u, name=%s, pwd=%s", id, name.c_str(), pwd.c_str());
    return id != 0 && !name.empty() && !pwd.empty();
}

// RPC 方法回调 — 框架收到远程请求后调用
void UserService::Login(::google::protobuf::RpcController *controller,
                        const ::fixbug::LoginRequest *request,
                        ::fixbug::LoginResponse *response,
                        ::google::protobuf::Closure *done)
{
    // 1. 从 request 中解出参数
    std::string name = request->name();
    std::string pwd  = request->pwd();

    // 2. 调用本地业务方法
    bool result = Login_local(name, pwd);

    // 3. 填充 response
    response->mutable_result()->set_errorcode(0);
    response->mutable_result()->set_errormsg(result ? "login success!" : "login failed!");
    response->set_success(result);

    // 4. 执行回调，通知框架发送响应
    done->Run();
}

void UserService::Register(::google::protobuf::RpcController* controller,
                           const ::fixbug::RegisterRequest* request,
                           ::fixbug::RegisterResponse* response,
                           ::google::protobuf::Closure* done)
{
    uint32_t id   = request->id();
    std::string name = request->name();
    std::string pwd  = request->pwd();

    bool result = Register_local(id, name, pwd);

    response->mutable_result()->set_errorcode(result ? 0 : 1);
    response->mutable_result()->set_errormsg(result ? "" : "invalid registration information");
    response->set_success(result);

    done->Run();
}
```

#### 2.2 编写 Provider 主入口

```cpp
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "userservice.h"    // 你自己写的业务类
#include "friendservice.h"  // 可以注册多个服务

int main(int argc, char **argv)
{
    // 1. 框架初始化（解析 -i 配置文件）
    MprpcApplication::Init(argc, argv);

    // 2. 创建 RPC 服务提供者
    RpcProvider provider;

    // 3. 注册 RPC 服务（可注册多个）
    provider.NotifyService(new UserService());
    provider.NotifyService(new FriendService());

    // 4. 启动服务（进入事件循环）
    //    内部操作：
    //      - 启动 muduo TcpServer 监听端口
    //      - 连接 Zookeeper，将服务/方法注册到 ZK
    provider.Run();

    return 0;
}
```

#### 2.3 编译服务端

`example/callee/CMakeLists.txt`（以 UserService 为例）：

```cmake
set(SRC_LIST userservice.cc ../user.pb.cc)
add_executable(provider ${SRC_LIST})
target_link_libraries(provider mprpc protobuf)
```

```bash
bash autobuild.sh
```

运行：

```bash
./bin/provider -i test.conf
```

**服务端启动干了三件事**（详见 Q3）：
1. 启动 muduo TcpServer 监听 `rpcserverip:rpcserverport`
2. 连接 Zookeeper，在 ZK 上创建 Znode：`/UserServiceRpc/Login` → `127.0.0.1:8000`
3. 进入事件循环，等待远程调用

---

### 第三步：实现 RPC 客户端（Consumer）

```cpp
#include <iostream>
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "user.pb.h"    // protoc 生成的桩代码

int main(int argc, char **argv)
{
    // 1. 框架初始化（加载配置文件，获取 ZK 地址等）
    MprpcApplication::Init(argc, argv);

    // 2. 创建 RPC Channel
    //     Channel 构造函数中连接 Zookeeper
    MprpcChannel channel;
    // 或用智能指针：auto channel = std::make_unique<MprpcChannel>();

    // 3. 创建 Stub（protoc 生成的客户端存根）
    //    Stub 内部持有 channel 指针，通过它发送请求
    fixbug::UserServiceRpc_Stub stub(&channel);

    // 4. 构造请求参数
    fixbug::LoginRequest request;
    request.set_name("test_user");
    request.set_pwd("123456");

    // 5. 准备响应对象
    fixbug::LoginResponse response;

    // 6. 创建 Controller（用于获取调用状态和错误信息）
    MprpcController controller;

    // 7. 发起远程调用（看起来就像调用本地函数！）
    stub.Login(&controller, &request, &response, nullptr);

    // 8. 处理结果
    if (controller.Failed()) {
        // 框架层面错误（网络、序列化、ZK 服务发现失败等）
        std::cout << "RPC call failed: " << controller.ErrorText() << std::endl;
    } else {
        if (response.success()) {
            std::cout << "Login success!" << std::endl;
        } else {
            std::cout << "Login failed: " << response.result().errormsg() << std::endl;
        }
    }

    return 0;
}
```

**编译客户端**：

`example/caller/CMakeLists.txt`：

```cmake
set(SRC_LIST calluserservice.cc ../user.pb.cc)
add_executable(consumer ${SRC_LIST})
target_link_libraries(consumer mprpc protobuf)
```

```bash
bash autobuild.sh
```

运行（需要先启动 Provider 和 Zookeeper）：

```bash
./bin/consumer -i test.conf
```

---

## Q12: `RpcProvider::NotifyService` 内部做了什么？服务方法是怎么被"注册"的？

**A:** `NotifyService()` 将服务对象和方法信息缓存在内存中，`Run()` 时才实际注册到 Zookeeper。

### NotifyService 源码解析

```cpp
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;

    // 通过 protobuf 反射获取服务的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();

    // 获取服务名称（如 "UserServiceRpc"）
    std::string service_name = pserviceDesc->name();

    // 遍历该服务的所有方法
    int methodCnt = pserviceDesc->method_count();
    for (int i = 0; i < methodCnt; ++i) {
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();

        // 存储 方法名 → MethodDescriptor 的映射
        service_info.m_methodMap.insert({method_name, pmethodDesc});
    }

    // 存储服务对象
    service_info.m_service = service;

    // 存储到全局 map：服务名 → ServiceInfo
    m_serviceMap.insert({service_name, service_info});
}
```

### 注册了什么

| 数据 | 来源 | 用途 |
|------|------|------|
| `service_name` | `ServiceDescriptor::name()` | ZK 节点路径、请求路由 |
| `method_name` | `MethodDescriptor::name()` | ZK 节点路径、请求路由 |
| `MethodDescriptor*` | protobuf 反射 | 调用时通过 `CallMethod()` 分发 |
| `Service*` | 用户传入的对象指针 | 调用时执行具体业务逻辑 |

### Zookeeper 上的注册（在 Run() 中）

```
RpcProvider::Run() 被调用
    │
    ├── 1. 启动 muduo TcpServer 监听端口
    │
    ├── 2. 连接 Zookeeper
    │      m_zkclient.Start()
    │
    ├── 3. 遍历 m_serviceMap
    │      │
    │      ├── 对每个 service_name:
    │      │     Create("/UserServiceRpc")           // 持久节点
    │      │     │
    │      │     └── 对每个 method_name:
    │      │           Create("/UserServiceRpc/Login", "127.0.0.1:8000", ZOO_EPHEMERAL)  // 临时节点
    │      │
    │      └── ...
    │
    └── 4. server.start() + m_eventloop.loop()      // 进入事件循环
```

---

## Q13: 远程方法是怎么被"调用"的？从 Consumer 的 `stub.Login()` 到 Provider 的 `UserService::Login()` 完整链路是什么？

**A:** 一次完整的 RPC 调用经历 **8 个阶段**：

```
┌─────────────────────────────────────────────────────────────────────┐
│ Consumer 端                               Provider 端               │
│                                                                     │
│  stub.Login()                                                      │
│      │                                                             │
│      ▼                                                             │
│  MprpcChannel::CallMethod()                 muduo TcpServer        │
│      │                                         │                   │
│      │  ① 构建请求数据                               │                   │
│      │  ② 从 ZK 获取服务地址                         │                   │
│      │  ③ connect + send                          │                   │
│      └──────────── 网络传输 ──────────────────────► │                   │
│                                                   │                   │
│                                                   ▼                   │
│                                               RpcProvider::OnMessage()
│                                                   │                   │
│                                                   │  ④ 解析 header    │
│                                                   │  ⑤ 反序列化参数    │
│                                                   │  ⑥ 反射调用        │
│                                                   ▼                   │
│                                             UserService::Login()     │
│                                                   │                   │
│                                                   │  ⑦ 执行业务       │
│                                                   │  ⑧ 序列化响应并发送│
│      ◄──────────── 网络传输 ──────────────────────┤                   │
│      │                                                             │
│      ▼                                                             │
│  解析响应，填充 response                                             │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 阶段详解

#### 阶段 ① — Consumer 端：序列化请求

`MprpcChannel::CallMethod()` 中：

```cpp
// 1. 获取服务名和方法名
const google::protobuf::ServiceDescriptor* sd = method->service();
std::string service_name = sd->name();
std::string method_name  = method->name();

// 2. 序列化请求参数
request->SerializeToString(&args_str);

// 3. 构造 RPC Header（protobuf 消息）
mprpc::RpcHeader rpcHeader;
rpcHeader.set_service_name(service_name);
rpcHeader.set_method_name(method_name);
rpcHeader.set_args_size(args_str.size());
rpcHeader.SerializeToString(&rpc_header_str);

// 4. 组装发送数据（自定义协议）
//     | header_size(4B) | rpc_header_str | args_str |
uint32_t header_size = rpc_header_str.size();
send_rpc_str.insert(0, std::string((char*)&header_size, 4));
send_rpc_str += rpc_header_str;
send_rpc_str += args_str;
```

**自定义协议格式**：

```
 0                   4                   4+header_size
 ├───────────────────┬───────────────────┬───────────────────────────┤
 │  header_size      │  rpc_header_str   │  args_str                 │
 │  (uint32_t, 4B)   │  (protobuf 序列化) │  (request 序列化后的数据)  │
 ├───────────────────┴───────────────────┴───────────────────────────┤
                     ↑                        ↑
                service_name             LoginRequest 的
                method_name              protobuf 二进制
                args_size
```

#### 阶段 ② — Consumer 端：从 ZK 获取服务地址

```cpp
// 从 ZK 查找: /UserServiceRpc/Login  →  "127.0.0.1:8000"
std::string method_path = "/" + service_name + "/" + method_name;
std::string host_data = m_zkclient.GetData(method_path.c_str());

// 解析 IP:Port
int idx = host_data.find(":");
std::string ip   = host_data.substr(0, idx);
uint16_t port    = atoi(host_data.substr(idx + 1).c_str());
```

#### 阶段 ③ — Consumer 端：TCP 发送

```cpp
int clientfd = socket(AF_INET, SOCK_STREAM, 0);
connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0);
```

#### 阶段 ④ — Provider 端：接收并解析 Header

`RpcProvider::OnMessage()` 中：

```cpp
// 读取 header_size（前 4 字节）
uint32_t header_size = 0;
recv_buf.copy((char*)&header_size, 4, 0);

// 读取 header_str，反序列化得到 service_name、method_name、args_size
std::string rpc_header_str = recv_buf.substr(4, header_size);
mprpc::RpcHeader rpcHeader;
rpcHeader.ParseFromString(rpc_header_str);

std::string service_name = rpcHeader.service_name();
std::string method_name  = rpcHeader.method_name();
uint32_t args_size       = rpcHeader.args_size();
```

#### 阶段 ⑤ — Provider 端：反序列化请求参数

```cpp
std::string args_str = recv_buf.substr(4 + header_size, args_size);
google::protobuf::Message* request = service->GetRequestPrototype(method).New();
request->ParseFromString(args_str);
```

#### 阶段 ⑥ — Provider 端：反射调用业务方法

```cpp
// service 就是用户注册的 UserService 对象
// method 是对应方法的 MethodDescriptor
service->CallMethod(method, controller, request, response, done);
```

`CallMethod()` 是 protobuf 的反射机制，内部通过 `switch` 或函数指针跳转到 `UserService::Login()` 或 `UserService::Register()`。

#### 阶段 ⑦ — Provider 端：执行业务逻辑

框架调用到 `UserService::Login()` 中，开发者在此实现自己的业务逻辑。

#### 阶段 ⑧ — Provider 端：序列化并发送响应

`SendRpcResponse()` 中：

```cpp
// 序列化 response
response->SerializeToString(&response_str);

// 构造响应头
mprpc::RpcHeader rpcHeader;
rpcHeader.set_args_size(response_str.size());
rpcHeader.SerializeToString(&rpc_header_str);

// 组装并发送（和请求协议格式相同）
uint32_t header_size = rpc_header_str.size();
send_rpc_str.insert(0, std::string((char*)&header_size, 4));
send_rpc_str += rpc_header_str;
send_rpc_str += response_str;
conn->send(send_rpc_str);
```

Consumer 端收到响应后，按同样的协议格式解析，将结果填充到 `response` 对象中，`stub.Login()` 返回。

---

## Q14: `MprpcChannel` 和 `MprpcController` 分别起什么作用？

### MprpcChannel

`MprpcChannel` 继承自 `google::protobuf::RpcChannel`，是客户端 RPC 调用的核心通道：

```cpp
class MprpcChannel : public google::protobuf::RpcChannel
{
public:
    MprpcChannel();                                  // 连接 Zookeeper
    void CallMethod(...) override;                    // 序列化 → 服务发现 → 发送 → 接收 → 反序列化
private:
    ZKClient m_zkclient;                             // 持有 ZK 客户端，用于服务发现
};
```

- Stub 内部持有 `RpcChannel*`，每次调用 `stub.Login()` 最终由 `channel->CallMethod()` 执行网络通信。
- 一个 `MprpcChannel` 在构造时连接一次 Zookeeper，适用于多次远程调用。

### MprpcController

`MprpcController` 继承自 `google::protobuf::RpcController`，用于传递 RPC 调用的状态和错误信息：

```cpp
class MprpcController : public google::protobuf::RpcController
{
public:
    bool Failed() const;                 // 调用是否失败
    std::string ErrorText() const;       // 错误描述
    void SetFailed(const std::string& reason);  // 设置错误
    void SetFailed(const RpcError& error);      // 带错误码的设置
    const RpcError* GetError() const;           // 获取详细错误码
    ...
};
```

典型用法：

```cpp
MprpcController controller;
stub.Login(&controller, &request, &response, nullptr);

if (controller.Failed()) {
    // 处理错误（网络错误、ZK 服务发现失败、序列化失败等）
    std::cerr << controller.ErrorText() << std::endl;
}
```

---

## Q15: 如果要添加一个新的 RPC 服务和对应的客户端调用，完整的操作步骤是什么？

以添加一个 `OrderService`（订单服务）为例：

### 步骤 1：定义 `.proto` 文件

```protobuf
syntax = "proto3";
package fixbug;
option cc_generic_services = true;

message CreateOrderRequest {
    uint32 user_id = 1;
    string product = 2;
    uint32 amount  = 3;
}

message CreateOrderResponse {
    ResultCode result = 1;
    uint32 order_id   = 2;
    bool success      = 3;
}

service OrderServiceRpc {
    rpc CreateOrder(CreateOrderRequest) returns (CreateOrderResponse);
}
```

### 步骤 2：生成 C++ 代码

```bash
protoc --proto_path=. --cpp_out=. order.proto
```

### 步骤 3：实现服务端业务类

```cpp
// orderservice.h
class OrderService : public fixbug::OrderServiceRpc
{
public:
    bool CreateOrder_local(uint32_t user_id, const std::string& product, uint32_t amount);
    
    void CreateOrder(::google::protobuf::RpcController* controller,
                     const ::fixbug::CreateOrderRequest* request,
                     ::fixbug::CreateOrderResponse* response,
                     ::google::protobuf::Closure* done) override;
};

// orderservice.cc
void OrderService::CreateOrder(::google::protobuf::RpcController* controller,
                                const ::fixbug::CreateOrderRequest* request,
                                ::fixbug::CreateOrderResponse* response,
                                ::google::protobuf::Closure* done)
{
    uint32_t user_id = request->user_id();
    std::string product = request->product();
    uint32_t amount = request->amount();

    bool result = CreateOrder_local(user_id, product, amount);

    response->mutable_result()->set_errorcode(result ? 0 : 1);
    response->set_order_id(result ? 10001 : 0);
    response->set_success(result);

    done->Run();
}
```

### 步骤 4：向 Provider 注册新服务

在 `main.cc` 中添加一行：

```cpp
provider.NotifyService(new OrderService());
```

Zookeeper 会自动创建节点：

```
/OrderServiceRpc/CreateOrder  →  "127.0.0.1:8000"
```

### 步骤 5：实现客户端调用

```cpp
MprpcChannel channel;
fixbug::OrderServiceRpc_Stub stub(&channel);

fixbug::CreateOrderRequest request;
request.set_user_id(100);
request.set_product("MacBook Pro");
request.set_amount(1);

fixbug::CreateOrderResponse response;
MprpcController controller;

stub.CreateOrder(&controller, &request, &response, nullptr);

if (!controller.Failed() && response.success()) {
    std::cout << "Order created: " << response.order_id() << std::endl;
}
```

### 步骤 6：编译运行

```bash
bash autobuild.sh

# 终端 1：启动 Zookeeper
sudo systemctl start zookeeper

# 终端 2：启动 Provider
./bin/provider -i test.conf

# 终端 3：运行 Consumer
./bin/consumer -i test.conf
```

---

## Q16: 整体架构总结

```
┌─────────────────────────────────────────────────────────────────┐
│                     MPRPC 架构总览                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────┐    ┌──────────────┐    ┌──────────┐              │
│  │ Consumer  │    │  Zookeeper   │    │ Provider  │              │
│  │ (Client)  │    │  (注册中心)   │    │ (Server)  │              │
│  └─────┬────┘    └──────┬───────┘    └─────┬────┘              │
│        │                │                   │                    │
│        │  ① 注册服务    │                   │                    │
│        │◄───────────────┤◄──────────────────│                    │
│        │ 「/Service/Method → IP:Port」       │                    │
│        │                │                   │                    │
│        │  ② 发现地址    │                   │                    │
│        ├───────────────►│                   │                    │
│        │◄───────────────┤                   │                    │
│        │                │                   │                    │
│        │  ③ TCP 通信    │                   │                    │
│        ├────────────────────────────────────►│                    │
│        │◄────────────────────────────────────┤                    │
│        │                │                   │                    │
└─────────────────────────────────────────────────────────────────┘

核心组件：
├── libmprpc.a          ── 静态库，服务端和客户端都链接它
├── MprpcApplication    ── 框架初始化（配置加载、单例管理）
├── MprpcConfig         ── 配置文件解析器
├── RpcProvider         ── 服务提供者（muduo TcpServer + ZK 注册）
├── MprpcChannel        ── RPC 通信通道（ZK 服务发现 + TCP 通信）
├── MprpcController     ── RPC 调用状态和错误管理
├── ZKClient            ── Zookeeper C API 封装
├── Logger              ── 异步日志系统
└── RpcError            ── 结构化错误码体系
```

---

## Q17: 当前项目对 Zookeeper 做了哪些容错处理？有哪些缺失？

**A:** 下面从 **ZK 连接、服务注册、服务发现、会话管理、通信** 五个维度逐一评估。

---

### 一、现有容错措施盘点

| 场景 | 代码位置 | 现有处理 | 处理方式 |
|------|---------|---------|---------|
| ZK 地址不可达（`zookeeper_init` 返回 null） | `zookeeperutil.cc:41-48` | ✅ 有 | 打印错误 → `exit(EXIT_FAILURE)` |
| `sem_wait` 永久阻塞（ZK 永远连不上） | `zookeeperutil.cc:52` | ❌ 无超时 | 一直挂起，进程无法退出 |
| Znode 创建失败 | `zookeeperutil.cc:105-111` | ✅ 有 | 打印错误 → `exit(EXIT_FAILURE)` |
| 父节点不存在（递归创建） | `zookeeperutil.cc:56-87` | ✅ 有 | 递归创建父节点 |
| 父节点创建失败 | `zookeeperutil.cc:79-85` | ⚠️ 仅打日志 | 只打印 ERROR，不中断 |
| ZK 句柄为 null 时调用 GetData | `zookeeperutil.cc:118-126` | ✅ 有 | 返回空字符串 |
| `zoo_get` 读取节点数据失败 | `zookeeperutil.cc:132-137` | ✅ 有 | 返回空字符串 |
| Consumer 获取空地址 | `mprpcchannel.cc:126-131` | ✅ 有 | 通过 `controller->SetFailed()` 上报错误 |
| Consumer 解析 IP:Port 格式错误 | `mprpcchannel.cc:134-141` | ✅ 有 | 通过 `controller->SetFailed()` 上报错误 |
| Consumer TCP connect 失败 | `mprpcchannel.cc:151-161` | ✅ 有 | 通过 `controller->SetFailed()` 上报错误 |
| Consumer send/recv 失败 | `mprpcchannel.cc:165-190` | ✅ 有 | 通过 `controller->SetFailed()` 上报错误 |
| Provider 反序列化 header 失败 | `mprpcprovider.cc:149-154` | ✅ 有 | 打日志后 return（不回复客户端） |
| Provider 找不到服务/方法 | `mprpcprovider.cc:160-179` | ✅ 有 | 打日志后 return |

---

### 二、缺失的容错能力

#### 缺失 1：ZK 会话过期与断线重连

**当前问题**：`global_watcher` 只处理了 `ZOO_CONNECTED_STATE`，完全忽略了 `ZOO_EXPIRED_SESSION_STATE`、`ZOO_CONNECTING_STATE`、`ZOO_AUTH_FAILED_STATE`。

```cpp
void global_watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            // ✅ 只处理了连接成功
            sem_t* sem = (sem_t*)zoo_get_context(zh);
            sem_post(sem);
        }
        // ❌ 缺少以下分支：
        //   ZOO_CONNECTING_STATE    → 连接中，等待重连
        //   ZOO_EXPIRED_SESSION_STATE → 会话过期，需要重新初始化
        //   ZOO_AUTH_FAILED_STATE   → 认证失败
    }
}
```

**后果**：
- ZK 重启或网络抖动导致会话过期 → Provider 的临时节点自动删除 → Consumer 无法发现服务
- Provider 和 Consumer 的 `m_zhandle` 进入无效状态，后续操作全部失败
- 没有任何重连或恢复机制

#### 缺失 2：Provider 端 ZK 断线后不重新注册

**当前问题**：临时节点（`ZOO_EPHEMERAL`）随会话自动删除，但 Provider 的 watcher 中没有重新注册的逻辑。

**正常流程**：
```
Provider 启动 → ZK 连接 → 创建 /UserServiceRpc/Login (临时节点)
                                          ↓
ZK 宕机 → 会话断开 → 临时节点自动删除
                                          ↓
ZK 重启 → Provider 的 ZK 客户端自动重连 → 会话恢复
                                          ↓
   ❌ 但 Provider 没有重新创建临时节点！
   ❌ Consumer 仍然查询不到任何地址！
```

#### 缺失 3：Consumer 端服务发现无缓存和降级

**当前问题**：每次 `CallMethod()` 都直接调用 `m_zkclient.GetData()`，没有任何本地缓存或降级策略。

- ZK 短暂不可用 → 服务发现失败 → 调用直接失败
- 没有本地缓存上一次成功的地址作为降级方案
- 没有重试机制

#### 缺失 4：`sem_wait` 无超时保护

**当前问题**：`ZKClient::Start()` 中 `sem_wait(&m_sem)` 是阻塞等待，如果 ZK 服务器一直不响应，进程永久挂起。

```cpp
sem_wait(&m_sem);  // ❌ 如果 ZK 永远连不上，永不返回
```

#### 缺失 5：缺少主动心跳检测

**当前问题**：虽然 Zookeeper C API 底层有默认心跳（基于 `zookeeper_init` 的 `recv_timeout` 参数，当前设为 3000ms），但框架层面：

- 没有检测心跳超时的回调处理
- 没有心跳丢失后的重连策略
- Provider 没有定期向 ZK 上报健康状态
- Consumer 无法感知服务端的健康变化（只能被动等调用失败）

#### 缺失 6：Provider 端错误不回复客户端

**当前问题**：Provider 的 `OnMessage()` 在处理 header 解析失败、服务/方法找不到、参数解析失败时，**仅仅是 log + return**，没有向客户端发送任何错误响应。

```cpp
// mprpcprovider.cc:152
if (!rpcHeader.ParseFromString(rpc_header_str)) {
    // ❌ 只是 return，客户端会一直阻塞在 recv()
    return;
}
```

**后果**：Consumer 端的 `recv()` 会一直阻塞，直到触发系统 TCP 超时（可能数分钟）。

---

## Q18: 如何设计 Zookeeper 相关的容错机制？给出完整方案

**A:** 下面按优先级从高到低给出设计方案。

---

### 设计一：完善 Watcher 回调，支持会话恢复

修改 `global_watcher`，处理所有会话状态：

```cpp
void global_watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx) {
    if (type != ZOO_SESSION_EVENT) return;

    ZKClient* client = static_cast<ZKClient*>(watcherCtx);  // 传入 this 指针

    switch (state) {
    case ZOO_CONNECTED_STATE:
        LOG_INFO("ZK connected successfully");
        sem_post(client->getConnectSem());     // 唤醒 Start()
        client->setConnected(true);
        break;

    case ZOO_CONNECTING_STATE:
        LOG_INFO("ZK reconnecting...");
        break;

    case ZOO_EXPIRED_SESSION_STATE:
        LOG_ERROR("ZK session expired, reinitializing...");
        client->setConnected(false);
        client->reinitialize();                // 重新创建句柄 + 重新注册
        break;

    case ZOO_AUTH_FAILED_STATE:
        LOG_ERROR("ZK auth failed");
        client->setConnected(false);
        break;

    default:
        break;
    }
}
```

---

### 设计二：`ZKClient::Start()` 增加连接超时

```cpp
bool ZKClient::Start() {
    std::string conststr = host + ":" + port;
    m_zhandle = zookeeper_init(conststr.c_str(), global_watcher, 3000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle) {
        LOG_ERROR("zookeeper_init error");
        return false;          // 不再 exit，由上层决策
    }

    zoo_set_context(m_zhandle, this);

    // 带超时的等待
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;           // 5 秒超时
    int ret = sem_timedwait(&m_sem, &ts);
    if (ret == -1) {
        LOG_ERROR("ZK connect timeout after 5s");
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        return false;
    }

    LOG_INFO("zookeeper_init success");
    return true;
}
```

---

### 设计三：Provider 端会话过期后自动重新注册

```cpp
void ZKClient::reinitialize() {
    // 1. 关闭旧句柄
    if (m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    sem_destroy(&m_sem);
    sem_init(&m_sem, 0, 0);

    // 2. 重新连接
    if (!Start()) {
        LOG_ERROR("ZK reinitialize failed");
        return;
    }

    // 3. 重新注册所有服务（由 Provider 调用者注入回调）
    if (m_on_reconnect_cb) {
        m_on_reconnect_cb();    // 回调 RpcProvider 重新执行注册逻辑
    }
}
```

`RpcProvider` 中注入回调：

```cpp
void RpcProvider::Run() {
    // ... 原有逻辑 ...

    m_zkclient.Start();

    // 设置重连回调
    m_zkclient.setOnReconnectCallback([this]() {
        registerServicesToZK();   // 提取独立的注册方法
    });

    registerServicesToZK();       // 首次注册
    server.start();
    m_eventloop.loop();
}

void RpcProvider::registerServicesToZK() {
    for (auto &sp : m_serviceMap) {
        std::string service_path = "/" + sp.first;
        m_zkclient.Create(service_path.c_str(), nullptr, 0);
        for (auto &mp : sp.second.m_methodMap) {
            std::string method_path = service_path + "/" + mp.first;
            char data[128];
            sprintf(data, "%s:%d", ip.c_str(), port);
            m_zkclient.Create(method_path.c_str(), data, strlen(data), ZOO_EPHEMERAL);
        }
    }
}
```

---

### 设计四：Consumer 端添加本地缓存 + 重试 + 降级

```cpp
class MprpcChannel {
    // 添加本地缓存：{service_method → ip:port}
    std::unordered_map<std::string, std::string> m_cache;
    std::mutex m_cache_mtx;
    static constexpr int MAX_RETRY = 3;
};

void MprpcChannel::CallMethod(...) {
    std::string method_path = "/" + service_name + "/" + method_name;

    // 1. 优先查本地缓存
    std::string host_data;
    {
        std::lock_guard<std::mutex> lock(m_cache_mtx);
        auto it = m_cache.find(method_path);
        if (it != m_cache.end()) {
            host_data = it->second;
        }
    }

    // 2. 缓存未命中或验证失败，从 ZK 查询（带重试）
    if (host_data.empty()) {
        for (int i = 0; i < MAX_RETRY; ++i) {
            host_data = m_zkclient.GetData(method_path.c_str());
            if (!host_data.empty()) break;
            if (i < MAX_RETRY - 1) {
                LOG_WARN("ZK GetData failed, retry %d/%d", i + 1, MAX_RETRY);
                sleep(1);  // 退避
            }
        }
        if (!host_data.empty()) {
            std::lock_guard<std::mutex> lock(m_cache_mtx);
            m_cache[method_path] = host_data;
        }
    }

    // 3. 降级处理
    if (host_data.empty()) {
        // 即使 ZK 完全不可用，如果缓存中有旧数据，仍可尝试
        LOG_ERROR("service discovery failed for %s", method_path.c_str());
        controller->SetFailed("service discovery failed");
        return;
    }

    // ... 后续 connect/send/recv 逻辑 ...
}
```

---

### 设计五：Provider 端向客户端发送错误响应

修改 `OnMessage()`，在异常分支中回复错误信息，避免客户端永久阻塞：

```cpp
void RpcProvider::OnMessage(..., const muduo::net::TcpConnectionPtr &conn, ...) {
    // ... 解析 header ...

    if (!rpcHeader.ParseFromString(rpc_header_str)) {
        // ✅ 发送错误响应给客户端
        sendErrorResponse(conn, "parse rpc header error");
        return;
    }

    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap.end()) {
        sendErrorResponse(conn, "service " + service_name + " not found");
        return;
    }

    // ... 其他错误分支类似 ...
}

// 统一发送错误响应
void RpcProvider::sendErrorResponse(const muduo::net::TcpConnectionPtr& conn,
                                     const std::string& err_msg) {
    // 构造一个包含错误信息的 RPC 响应
    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name("");
    rpcHeader.set_method_name("");
    rpcHeader.set_args_size(0);

    std::string rpc_header_str;
    rpcHeader.SerializeToString(&rpc_header_str);

    std::string send_str;
    uint32_t header_size = rpc_header_str.size();
    send_str.insert(0, std::string((char*)&header_size, 4));
    send_str += rpc_header_str;

    conn->send(send_str);
    conn->shutdown();

    LOG_ERROR("Sent error response to client: %s", err_msg.c_str());
}
```

---

### 设计六：主动心跳检测

在 `ZKClient` 中增加心跳状态跟踪：

```cpp
class ZKClient {
    bool m_connected = false;           // 当前连接状态
    int64_t m_last_heartbeat_ts = 0;    // 最后心跳时间
    static constexpr int64_t HEARTBEAT_TIMEOUT_MS = 10000;  // 10秒无响应判为超时

public:
    bool isConnected() const { return m_connected; }
    void setConnected(bool v) {
        m_connected = v;
        if (v) m_last_heartbeat_ts = nowMs();
    }

    // 定期检查心跳（由外部定时器调用，如 muduo 的 Timer）
    void checkHealth() {
        if (!m_connected) {
            // 已断连，尝试重连
            reinitialize();
            return;
        }
        if (m_zhandle) {
            // 发送心跳探测
            int rc = zoo_exists(m_zhandle, "/", 0, nullptr);
            if (rc != ZOK) {
                LOG_WARN("ZK heartbeat check failed: %d", rc);
                setConnected(false);
                reinitialize();
            } else {
                m_last_heartbeat_ts = nowMs();
            }
        }
    }
};
```

在 `RpcProvider::Run()` 中注册定时心跳：

```cpp
// 每 5 秒检查一次 ZK 连接健康状态
m_eventloop.runEvery(5.0, std::bind(&ZKClient::checkHealth, &m_zkclient));
```

---

### 设计七：分层容错决策树

将上述容错机制整合为完整的决策流程：

```
Consumer 发起 RPC 调用
    │
    ├── 1. 查本地缓存
    │     ├─ 命中 → 直接使用缓存地址
    │     └─ 未命中 → 进入步骤 2
    │
    ├── 2. ZK 服务发现（最多重试 3 次）
    │     ├─ 成功 → 更新缓存 → 进行步骤 3
    │     └─ 全部失败 → 降级：
    │           ├─ 有旧缓存 → 用旧地址尝试（发出警告）
    │           └─ 无缓存 → 返回 SERVICE_DISCOVERY_FAILED
    │
    ├── 3. TCP 连接 Provider
    │     ├─ 成功 → 发送请求
    │     └─ 失败 → 清除该地址缓存 → 回到步骤 2 重试
    │
    └── 4. 接收响应
          ├─ 收到正常响应 → 返回给业务层
          ├─ 收到错误响应 → 解析错误码，返回给业务层
          └─ recv 超时 → 返回 RPC_CALL_TIMEOUT
```

---

---

## Q19: 除了缓存和重试，还有哪些容错措施可以保证项目稳定运行？

**A:** 以下给出 **8 个独立于缓存的容错措施**，综合运用后可显著提升系统韧性。

---

### 措施一：熔断器（Circuit Breaker）

防止 Consumer 持续向已故障的 Provider 发起无效调用，导致资源耗尽（雪崩效应）。

**设计思路**：在 `MprpcChannel` 中为每个方法路径维护一个熔断器状态机。

```
熔断器三态：
┌──────────┐   连续失败达阈值    ┌──────────┐
│  CLOSED  │ ──────────────────► │   OPEN   │
│ (正常)    │                    │ (熔断)    │
└────┬─────┘                    └────┬──────┘
     │                               │
     │ 成功调用重置                   │ 超时后进入
     │ 失败计数                      │
     │                               ▼
     │                        ┌──────────────┐
     └────────────────────────│  HALF_OPEN   │
         尝试成功，关闭熔断器     │ (半开，探活)  │
                               └──────────────┘
```

```cpp
class CircuitBreaker {
public:
    CircuitBreaker(int failure_threshold = 5, int timeout_seconds = 30)
        : m_failure_threshold(failure_threshold)
        , m_timeout_seconds(timeout_seconds)
        , m_failure_count(0)
        , m_state(CircuitState::CLOSED)
        , m_last_failure_time(0) {}

    enum class CircuitState { CLOSED, OPEN, HALF_OPEN };

    // 调用前检查是否允许放行
    bool allowRequest() {
        auto now = time(nullptr);
        switch (m_state) {
        case CircuitState::CLOSED:
            return true;
        case CircuitState::OPEN:
            if (now - m_last_failure_time >= m_timeout_seconds) {
                m_state = CircuitState::HALF_OPEN;  // 探活
                return true;
            }
            return false;
        case CircuitState::HALF_OPEN:
            return true;
        }
        return true;
    }

    // 记录成功调用
    void onSuccess() {
        m_failure_count = 0;
        if (m_state == CircuitState::HALF_OPEN) {
            LOG_INFO("Circuit breaker closed (recovered)");
            m_state = CircuitState::CLOSED;
        }
    }

    // 记录失败调用
    void onFailure() {
        m_failure_count++;
        m_last_failure_time = time(nullptr);
        if (m_failure_count >= m_failure_threshold) {
            LOG_ERROR("Circuit breaker opened (threshold=%d)", m_failure_threshold);
            m_state = CircuitState::OPEN;
        }
    }

private:
    int m_failure_threshold;
    int m_timeout_seconds;
    int m_failure_count;
    CircuitState m_state;
    time_t m_last_failure_time;
};
```

**在 `MprpcChannel` 中集成**：

```cpp
class MprpcChannel {
    // 每个方法路径对应一个熔断器
    std::unordered_map<std::string, std::unique_ptr<CircuitBreaker>> m_circuit_breakers;
};

void MprpcChannel::CallMethod(...) {
    std::string method_path = "/" + service_name + "/" + method_name;

    // 检查熔断器
    auto& cb = m_circuit_breakers[method_path];
    if (!cb) cb = std::make_unique<CircuitBreaker>();
    if (!cb->allowRequest()) {
        controller->SetFailed("circuit breaker open for " + method_path);
        return;
    }

    // ... 原有 service discovery + TCP 通信逻辑 ...

    if (/* 调用成功 */) {
        cb->onSuccess();
    } else {
        cb->onFailure();
    }
}
```

**优势**：Provider 故障后，Consumer 在 5 次连续失败后自动熔断，不再发出无效请求。30 秒后尝试一次探活，成功则自动恢复。

---

### 措施二：超时控制（Timeout Control）

当前 `recv()` 是阻塞的且没有超时，一旦 Provider 不回复，Consumer 线程永久挂起。

```cpp
// mprpcchannel.cc 改造：给 recv 加超时

// 设置 socket 接收超时
struct timeval tv;
tv.tv_sec  = 5;  // 5 秒超时
tv.tv_usec = 0;
setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

// 现在 recv 最多阻塞 5 秒
int recv_size = recv(clientfd, recv_buf, sizeof(recv_buf), 0);
if (recv_size == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 超时！
        controller->SetFailed("RPC call timeout (5s)");
        close(clientfd);
        return;
    }
    // 其他错误
}
```

**同时给 `CallMethod` 加整体超时**：

```cpp
// 在 CallMethod 开头记录时间
auto start_ts = std::chrono::steady_clock::now();
constexpr int64_t TOTAL_TIMEOUT_MS = 8000;  // 整体调用超时 8s

// 每次阻塞操作前检查
auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start_ts).count();
if (elapsed > TOTAL_TIMEOUT_MS) {
    controller->SetFailed("RPC call timeout (overall)");
    close(clientfd);
    return;
}
```

**超时链路覆盖**：

| 阶段 | 超时建议值 | 说明 |
|------|-----------|------|
| ZK 连接 | 5s | `sem_timedwait` |
| ZK GetData | 3s | `zoo_get` 内部超时 |
| TCP connect | 3s | `SO_SNDTIMEO` |
| TCP send | 3s | `SO_SNDTIMEO` |
| TCP recv | 5s | `SO_RCVTIMEO` |
| 整体 RPC | 8s | 从 `CallMethod` 开始到返回 |

---

### 措施三：异步非阻塞 RPC 调用

当前的 `CallMethod` 是同步阻塞的：connect → send → recv 全部串行阻塞。这意味着一路 RPC 调用占满一个线程 8 秒，QPS 极低。

**改为基于 muduo 的异步实现**：

```cpp
class MprpcChannel : public google::protobuf::RpcChannel {
    muduo::net::EventLoop* m_loop;           // 复用 muduo 事件循环
    muduo::net::TcpClientPtr m_client;        // 复用 muduo TcpClient
    std::unordered_map<int64_t, PendingCall> m_pending_calls;  // 待完成调用表
};

struct PendingCall {
    google::protobuf::RpcController* controller;
    google::protobuf::Message* response;
    google::protobuf::Closure* done;
    int64_t timeout_timer_id;
};

// 异步调用流程：
// 1. 从 ZK 获取地址
// 2. muduo::net::TcpClient 连接到 Provider（muduo 非阻塞）
// 3. 连接建立后，muduo 回调 OnConnection → 发送请求
// 4. 注册 OnMessage 等待响应
// 5. 收到响应后，反序列化 → 调用 done->Run() 唤醒业务层
// 6. 注册定时器，超时未收到响应则调用 controller->SetFailed
```

**优势**：
- 不阻塞业务线程，单线程可管理数千路并发 RPC 调用
- 天然支持超时（muduo `EventLoop::runAfter`）
- 与 Provider 端的 muduo 模型对称，架构统一

---

### 措施四：请求超时重试 + 指数退避（Retry with Exponential Backoff）

当前无重试，一次失败就返回。增加智能重试：

```cpp
class RetryPolicy {
public:
    RetryPolicy(int max_retries = 3)
        : m_max_retries(max_retries), m_attempt(0) {}

    bool shouldRetry() {
        return m_attempt < m_max_retries;
    }

    int getWaitMs() {
        // 指数退避：1s → 2s → 4s（带随机抖动防止惊群）
        int base = 1000 * (1 << m_attempt);  // 2^attempt 秒
        int jitter = rand() % 500;           // ±500ms 随机抖动
        m_attempt++;
        return base + jitter;
    }

    void reset() { m_attempt = 0; }

private:
    int m_max_retries;
    int m_attempt;
};
```

**退避时间曲线**：

```
第 1 次重试：等待 1.0s ~ 1.5s
第 2 次重试：等待 2.0s ~ 2.5s
第 3 次重试：等待 4.0s ~ 4.5s
```

**注意**：重试要考虑**幂等性**。如果 RPC 方法不是幂等的（如创建订单），重试可能导致重复下单。解决方案：

| 方案 | 说明 |
|------|------|
| 仅对幂等方法重试 | 查询类方法（GET）可以重试，写入类方法（POST）谨慎 |
| 业务层去重 | 请求中携带全局唯一 `request_id`，Provider 端去重 |
| 由业务层决定 | 在 `CallMethod` 中增加 `RetryPolicy` 参数 |

---

### 措施五：Provider 端优雅关闭（Graceful Shutdown）

当前 Provider 直接 `Ctrl+C` 退出时，muduo TcpServer 关闭连接，但 ZK 临时节点需要等待会话超时（3 秒）才被删除。这 3 秒内 Consumer 仍会尝试连接已关闭的服务。

**设计方案**：

```cpp
void RpcProvider::Run() {
    // ... 原有逻辑 ...

    // 注册 SIGINT / SIGTERM 信号处理
    muduo::net::SignalSet signals(m_eventloop.getLoop(), SIGINT, SIGTERM);
    signals.setCallback([this]() {
        LOG_INFO("Received shutdown signal, graceful shutdown...");

        // 1. 手动删除 ZK 临时节点（立即生效，不必等会话超时）
        for (auto &sp : m_serviceMap) {
            for (auto &mp : sp.second.m_methodMap) {
                std::string method_path = "/" + sp.first + "/" + mp.first;
                zoo_delete(m_zkclient.getHandle(), method_path.c_str(), -1);
            }
        }

        // 2. 停止接受新连接
        m_server.stop();

        // 3. 等待已建立的连接处理完（最多等待 10 秒）
        m_eventloop.getLoop()->quitAfter(10.0);
    });

    server.start();
    m_eventloop.loop();
}
```

**关闭流程时序**：

```
Ctrl+C
    │
    ├── 1. 立即删除 ZK 临时节点 → Consumer 立即感知，不再路由到此节点
    ├── 2. 停止 TcpServer → 不再接受新连接
    ├── 3. 等待正在处理中的请求完成（最多 10s）
    ├── 4. 关闭所有未完成的连接
    └── 5. 进程退出
```

---

### 措施六：负载均衡（Load Balancing）

当前每个方法只对应一个 Provider 地址。要实现高可用，同一种方法应部署多个 Provider 实例。

**利用 ZK 的 `GET_CHILDREN` 获取多实例**：

```
Zookeeper 节点结构（多实例版）：
/UserServiceRpc
    └── Login
        ├── 0000000001  →  "192.168.1.1:8000"  (临时顺序节点)
        ├── 0000000002  →  "192.168.1.2:8000"  (临时顺序节点)
        └── 0000000003  →  "192.168.1.3:8000"  (临时顺序节点)
```

**Provider 端**改为创建 **临时顺序节点**：

```cpp
// provider 创建 ZK 节点时使用 ZOO_EPHEMERAL | ZOO_SEQUENCE
// 这样 ZK 会自动在节点名后加递增序号，允许多个相同服务注册
m_zkclient.Create(method_path.c_str(), data, datalen,
                  ZOO_EPHEMERAL | ZOO_SEQUENCE);
```

**Consumer 端**获取子节点列表并选择：

```cpp
// 获取所有 Provider 实例
struct String_vector children;
zoo_get_children(m_zhandle, "/UserServiceRpc/Login", 0, &children);

// 选择策略：随机 or 轮询
int idx = rand() % children.count;
std::string child_path = std::string("/UserServiceRpc/Login/") + children.data[idx];

// 读取该实例的地址
std::string host_data = m_zkclient.GetData(child_path.c_str());
```

**负载均衡策略枚举**：

| 策略 | 实现 | 适用场景 |
|------|------|---------|
| 随机（Random） | `rand() % N` | 通用，各实例性能相当 |
| 轮询（Round Robin） | 原子计数器 `++ % N` | 通用，请求分布均匀 |
| 加权轮询（Weighted RR） | 按权重分配 | 异构服务器 |
| 一致性哈希（Consistent Hash） | 按请求参数 hash | 需要会话亲和性 |
| 最少连接（Least Connections） | 统计活跃连接数 | 长连接场景 |

**Consumer 端集成轮询选择器**：

```cpp
class ServiceSelector {
    std::unordered_map<std::string, std::vector<std::string>> m_instances;
    std::unordered_map<std::string, std::atomic<uint64_t>> m_round_robin_idx;
    std::mutex m_mtx;

public:
    // 从 ZK 拉取实例列表
    void refresh(const std::string& method_path) {
        String_vector children;
        if (zoo_get_children(m_zkhandle, method_path.c_str(), 0, &children) == ZOK) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_instances[method_path].clear();
            for (int i = 0; i < children.count; i++) {
                std::string full_path = method_path + "/" + children.data[i];
                std::string addr = getData(full_path);
                m_instances[method_path].push_back(addr);
            }
            deallocate_String_vector(&children);
        }
    }

    // 轮询选择一个
    std::string select(const std::string& method_path) {
        std::lock_guard<std::mutex> lock(m_mtx);
        auto& instances = m_instances[method_path];
        if (instances.empty()) return "";
        uint64_t idx = m_round_robin_idx[method_path]++ % instances.size();
        return instances[idx];
    }
};
```

---

### 措施七：限流（Rate Limiting）

防止单个 Consumer 打垮 Provider，或 Provider 自身过载。

**Provider 端：令牌桶限流**

```cpp
class TokenBucket {
public:
    TokenBucket(int rate, int capacity)
        : m_rate(rate), m_capacity(capacity), m_tokens(capacity)
    {
        m_last_refill = std::chrono::steady_clock::now();
    }

    bool tryAcquire() {
        refill();
        if (m_tokens < 1) return false;
        m_tokens--;
        return true;
    }

private:
    void refill() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_refill);
        int64_t new_tokens = elapsed.count() * m_rate / 1000;
        if (new_tokens > 0) {
            m_tokens = std::min(m_capacity, m_tokens + new_tokens);
            m_last_refill = now;
        }
    }

    int m_rate;        // 每秒放行请求数
    int m_capacity;    // 最大积攒令牌数
    int m_tokens;
    std::chrono::steady_clock::time_point m_last_refill;
};
```

**在 `RpcProvider::OnMessage` 中使用**：

```cpp
// 全局限流：每秒最多处理 10000 个请求
static TokenBucket g_limiter(10000, 10000);

void RpcProvider::OnMessage(...) {
    if (!g_limiter.tryAcquire()) {
        LOG_ERROR("Rate limit exceeded, dropping request");
        sendErrorResponse(conn, "server overload, try again later");
        return;
    }
    // ... 正常处理 ...
}
```

**限流粒度可选**：

| 粒度 | 实现方式 | 说明 |
|------|---------|------|
| 全局 | 单例 TokenBucket | 保护 Provider 整体不 overload |
| 服务级 | 按 service_name 分桶 | 防止某个热点服务影响其他服务 |
| 方法级 | 按 method_name 分桶 | 精细控制每个方法的调用频率 |
| IP 级 | 按客户端 IP 分桶 | 防止单客户端恶意调用 |

---

### 措施八：全链路追踪（Distributed Tracing）

分布式系统中，一次 RPC 调用经过 Consumer → ZK → Provider 多个节点，故障定位困难。引入 **trace_id** 贯穿整条调用链：

**在 RPC Header 中扩展 trace_id**：

```protobuf
message RpcHeader {
    string service_name = 1;
    string method_name = 2;
    uint32 args_size = 3;
    string trace_id = 4;     // 新增：全局调用链路 ID
    string parent_span = 5;  // 新增：父调用标识
}
```

**Consumer 端生成 trace_id**：

```cpp
std::string generateTraceId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    static std::atomic<uint64_t> seq{0};
    // 格式：{毫秒时间戳}-{进程ID}-{序列号}
    return std::to_string(ms) + "-" + std::to_string(getpid()) + "-" + std::to_string(seq++);
}
```

**日志系统集成 trace_id**：

```cpp
// logger.h 增加线程局部存储的 trace_id
class Logger {
    static thread_local std::string s_trace_id;
public:
    static void setTraceId(const std::string& id) { s_trace_id = id; }
    static const std::string& getTraceId() { return s_trace_id; }
};

// 日志宏自动附加 trace_id
#define LOG_INFO(fmt, ...) do { \
    Logger::log(INFO, "[%s] " fmt, Logger::getTraceId().c_str(), ##__VA_ARGS__); \
} while(0)
```

**故障排查示例**：

```
# 按 trace_id 聚合所有日志
grep "trace-1623456789-1234-1" /var/log/mprpc/*.log

# 输出：
# [2026-05-22 10:00:01.123] [INFO] [consumer] [trace-1623456789-1234-1] CallMethod: /UserServiceRpc/Login
# [2026-05-22 10:00:01.124] [INFO] [consumer] [trace-1623456789-1234-1] ZK GetData: /UserServiceRpc/Login → 192.168.1.1:8000
# [2026-05-22 10:00:01.125] [INFO] [consumer] [trace-1623456789-1234-1] TCP connecting to 192.168.1.1:8000
# [2026-05-22 10:00:06.130] [ERROR] [consumer] [trace-1623456789-1234-1] connect timed out (5s)

# → 快速定位：Provider 192.168.1.1:8000 网络不可达
```

---

### 综合运用：完整容错决策流

将上述所有措施整合为统一决策流：

```
Consumer 发起 RPC 调用
    │
    │  ┌────────────────────────────────────────────┐
    ├──┤ 步骤 0：生成 trace_id，注入 RPC Header      │
    │  └────────────────────────────────────────────┘
    │
    │  ┌────────────────────────────────────────────┐
    ├──┤ 步骤 1：检查熔断器                           │
    │  │  ├─ OPEN     → 快速失败，返回熔断错误        │
    │  │  ├─ HALF_OPEN → 放行一个探活请求             │
    │  │  └─ CLOSED   → 放行                        │
    │  └────────────────────────────────────────────┘
    │
    │  ┌────────────────────────────────────────────┐
    ├──┤ 步骤 2：Zookeeper 服务发现（带重试）          │
    │  │  ├─ 查本地缓存 → 命中则直接使用              │
    │  │  ├─ 查 ZK（最多 3 次，指数退避 1s/2s/4s）    │
    │  │  │   └─ 全部失败且无缓存 → 熔断器记录失败     │
    │  │  │                               → 返回错误 │
    │  │  ├─ 负载均衡：从多实例中选一个               │
    │  │  └─ 更新本地缓存                            │
    │  └────────────────────────────────────────────┘
    │
    │  ┌────────────────────────────────────────────┐
    ├──┤ 步骤 3：TCP 通信（带超时）                    │
    │  │  ├─ socket 创建                             │
    │  │  ├─ connect 超时 3s                         │
    │  │  │   └─ 失败 → 清除该实例缓存 → 切下一实例   │
    │  │  ├─ send 超时 3s                            │
    │  │  │   └─ 失败 → 上报错误                     │
    │  │  └─ recv 超时 5s（整体调用不超 8s）           │
    │  │      └─ 超时 → 熔断器记录失败 → 重试或降级    │
    │  └────────────────────────────────────────────┘
    │
    │  ┌────────────────────────────────────────────┐
    ├──┤ 步骤 4：结果处理                             │
    │  │  ├─ 成功 → 熔断器记录成功（CLOSED）           │
    │  │  ├─ 业务错误 → 返回业务层处理                │
    │  │  └─ 系统错误 → 熔断器记录失败                 │
    │  └────────────────────────────────────────────┘
    │
    ▼
返回给业务层
```

---

### 容错措施全景对比

| 措施 | 解决的核心问题 | 对稳定性的贡献 | 实现复杂度 |
|------|--------------|--------------|-----------|
| **熔断器** | 防止连锁雪崩 | Consumer 快速失败，不浪费资源 | 低 |
| **超时控制** | 线程永久阻塞 | 线程不会卡死，释放资源 | 低 |
| **异步非阻塞** | 线程利用率低 | 单机 QPS 提升 10~100 倍 | 高 |
| **重试+指数退避** | 瞬态故障 | 容忍网络抖动，提高成功率 | 低 |
| **优雅关闭** | 服务停止时的请求中断 | 零停机迁移，请求不丢 | 中 |
| **负载均衡** | 单点故障 | 水平扩展，任意实例宕机不影响整体 | 中 |
| **限流** | Provider 过载 | 保障 Provider 不被打垮，维持服务质量 | 低 |
| **全链路追踪** | 故障定位困难 | 分钟级定位问题根因 | 中 |
| **服务发现缓存** | ZK 不可用 | ZK 抖动时 Consumer 仍能调用 | 低 |
| **会话重连+重新注册** | ZK 宕机恢复 | ZK 恢复后服务自动恢复 | 中 |

---

## Q20: 遇到 ZK 宕机或网络分区时，系统应该怎么表现？（降级策略）

**A:** 根据不同的可用性要求，提供 3 个级别的降级策略。

---

### 策略 A：优雅降级（推荐）

```
ZK 宕机或网络分区
    │
    ├── Consumer 端：
    │     ├─ 使用本地缓存的服务地址继续调用
    │     ├─ 每次调用记录 WARN 日志
    │     ├─ 与 Provider 直连（跳过 ZK）
    │     └─ 每 30 秒尝试重连 ZK 一次
    │
    └── Provider 端：
          ├─ 临时节点不被删除（会话未超时时）
          ├─ 如果会话已过期 → 本地服务继续运行
          │    （但 Consumer 发现不了，需配合 Consumer 端缓存）
          └─ ZK 恢复后自动重新注册
```

**Consumer 端实现**：

```cpp
class MprpcChannel {
    bool m_zk_available = true;   // ZK 是否可用
    std::unordered_map<std::string, std::string> m_addr_cache;  // 持久化地址缓存
    std::string m_cache_file = "/tmp/mprpc_addr_cache.dat";     // 磁盘缓存（进程重启不丢失）
};

void MprpcChannel::CallMethod(...) {
    // ZK 可用时：正常查询 + 更新缓存
    if (m_zk_available) {
        host_data = m_zkclient.GetData(method_path.c_str());
        if (!host_data.empty()) {
            saveToCache(method_path, host_data);
        } else {
            // ZK 返回空 → 可能 ZK 挂了
            m_zk_available = false;
            host_data = loadFromCache(method_path);  // 降级：读磁盘缓存
            LOG_WARN("ZK unavailable, using cached address for %s", method_path.c_str());
        }
    } else {
        // ZK 不可用时：只读缓存
        host_data = loadFromCache(method_path);
        // 每隔 N 次调用尝试一次 ZK
        static std::atomic<int> probe_count{0};
        if (probe_count++ % 100 == 0) {
            std::string probe = m_zkclient.GetData(method_path.c_str());
            if (!probe.empty()) {
                m_zk_available = true;
                saveToCache(method_path, probe);
                host_data = probe;
                LOG_INFO("ZK recovered, restored service discovery");
            }
        }
    }

    if (host_data.empty()) {
        controller->SetFailed("service unavailable (no cached address)");
        return;
    }
}
```

---

### 策略 B：直连模式（Fallback to Direct Connect）

用户可以在配置文件中指定直连地址，绕过 ZK：

```conf
# test.conf — 增加直连配置
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181

# 降级配置：当 ZK 不可用时，直接连接以下地址
fallback_ip=127.0.0.1
fallback_port=8000
```

```cpp
void MprpcChannel::CallMethod(...) {
    std::string host_data = m_zkclient.GetData(method_path.c_str());

    if (host_data.empty()) {
        // 降级：读 fallback 配置
        std::string fb_ip = MprpcApplication::GetConfig().Load("fallback_ip");
        std::string fb_port = MprpcApplication::GetConfig().Load("fallback_port");
        if (!fb_ip.empty() && !fb_port.empty()) {
            host_data = fb_ip + ":" + fb_port;
            LOG_WARN("ZK unavailable, using fallback address %s", host_data.c_str());
        } else {
            controller->SetFailed("service discovery failed and no fallback configured");
            return;
        }
    }
    // ... 解析 host_data，connect ...
}
```

---

### 策略 C：定期巡检 + 自动恢复脚本

在系统层面部署 ZK 健康巡检，确保 ZK 故障时能自动拉起：

```bash
#!/bin/bash
# /usr/local/bin/zk_health_check.sh
# 每 10 秒检查一次 ZK 健康状态

ZK_HOST="127.0.0.1"
ZK_PORT="2181"

while true; do
    # 通过 ZK 的四字命令检查状态
    echo "ruok" | nc -w 3 $ZK_HOST $ZK_PORT 2>/dev/null | grep -q "imok"
    if [ $? -ne 0 ]; then
        echo "[$(date)] ZK is down, attempting restart..."
        sudo systemctl restart zookeeper
        # 等待 ZK 启动后，通知所有 Provider 重新注册
        sleep 5
        # 触发所有 MPRPC Provider 重连（发送 SIGHUP 信号）
        pkill -HUP provider 2>/dev/null || true
    fi
    sleep 10
done
```

---

## Q21: 上述所有容错措施的代码侵入性和优先级建议

### 实施优先级路线图

| 阶段 | 措施 | 预估工时 | 收益 |
|------|------|---------|------|
| **Phase 1（P0）** | `sem_timedwait` 超时 | 0.5 天 | 解决进程永久挂起 |
| | recv 超时 + SO_RCVTIMEO | 0.5 天 | 解决调用线程永久阻塞 |
| | Provider 错误回复客户端 | 1 天 | 解决 Consumer 端 recv 阻塞 |
| | Watcher 会话过期处理 | 1 天 | 解决 ZK 重连后服务不可用 |
| **Phase 2（P1）** | 熔断器 | 1 天 | 防止雪崩 |
| | 重试 + 指数退避 | 0.5 天 | 容忍网络抖动 |
| | 优雅关闭 | 1 天 | 零停机部署 |
| | 服务发现缓存 | 1 天 | ZK 抖动不中断 |
| **Phase 3（P2）** | 限流 | 1 天 | Provider 稳定性 |
| | 负载均衡 | 2 天 | 水平扩展能力 |
| | 全链路追踪 | 2 天 | 可观测性 |
| **Phase 4（P3）** | 异步非阻塞 | 5 天 | 极致性能 |

### 代码侵入性评估

| 措施 | 侵入范围 | 是否需要改 proto |
|------|---------|----------------|
| 超时控制 | `mprpcchannel.cc` 仅几行 | 否 |
| 熔断器 | `MprpcChannel` 新增类 | 否 |
| 优雅关闭 | `mprpcprovider.cc` 加信号处理 | 否 |
| 重试+退避 | `MprpcChannel::CallMethod` 改造 | 否 |
| 限流 | `RpcProvider::OnMessage` 加几行 | 否 |
| 负载均衡 | `MprpcChannel` + `zookeeperutil` 改造 | 否 |
| 全链路追踪 | `Logger` + `RpcHeader` | **是**（扩展 proto） |
| 异步非阻塞 | `MprpcChannel` 重写 | 否（重新实现接口） |
| 会话重连 | `zookeeperutil.cc` 改造 | 否 |
| Provider 错误回复 | `mprpcprovider.cc` 改造 | 否 |

---

## 参考来源

- 项目代码：`src/zookeeperutil.h`、`src/zookeeperutil.cc`、`src/mprpcprovider.cc`、`src/mprpcchannel.cc`、`src/mprpcapplication.cc`、`src/include/rpcerror.h`、`src/include/logger.h`、`example/**/*`
- 配置文件：`test.conf`
- 构建文件：`example/callee/CMakeLists.txt`、`example/caller/CMakeLists.txt`
- ZooKeeper C API 文档：`/usr/include/zookeeper/zookeeper.h`
- [Apache ZooKeeper 官方文档](https://zookeeper.apache.org/doc/current/)
- [Protocol Buffers C++ RPC 实现](https://protobuf.dev/reference/cpp/cpp-generated/#service)
- [Martin Fowler - Circuit Breaker](https://martinfowler.com/bliki/CircuitBreaker.html)
- [Google SRE 手册 - 过载保护与优雅降级](https://sre.google/sre-book/handling-overload/)
