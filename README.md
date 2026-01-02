# README.md - MPRPC 分布式网络通信框架

# MPRPC - C++分布式网络通信框架

一个基于muduo网络库和protobuf序列化的高性能RPC通信框架，实现了分布式系统中远程过程调用的核心机制。

## 📋 项目概述

MPRPC框架旨在提供高效、可靠的分布式服务通信能力，通过封装底层网络通信和序列化细节，让开发者能够像调用本地函数一样调用远程服务。

### 核心特性

- **高性能网络通信**：基于muduo网络库的Reactor模式实现高效TCP通信
- **高效数据序列化**：使用Protobuf实现跨平台数据序列化/反序列化
- **服务注册与发现**：集成Zookeeper实现服务自动注册与发现
- **配置化管理**：支持灵活的配置文件管理
- **完善的日志系统**：提供详细的日志记录功能
- **简单易用API**：类似本地函数调用的简洁API设计

## 📁 项目结构

```
MPRPC/
├── autobuild.sh          # 项目构建脚本
├── CMakeLists.txt        # CMake构建配置
├── README.md             # 项目说明文档
├── test.conf             # 配置文件示例
├── bin/                  # 生成的可执行文件
├── build/                # 构建目录
├── example/              # 使用示例
│   ├── callee/           # 服务提供者示例
│   │   ├── friendservice.cc
│   │   └── userservice.cc
│   ├── caller/           # 服务调用者示例
│   │   ├── callfriendservice.cc
│   │   └── calluserservice.cc
│   ├── friend.pb.cc      # 自动生成的Friend服务代码
│   ├── friend.pb.h
│   ├── friend.proto      # Friend服务定义
│   ├── user.pb.cc        # 自动生成的User服务代码
│   ├── user.pb.h
│   └── user.proto        # User服务定义
├── lib/                  # 生成的静态/动态库
│   ├── include/          # 头文件目录
│   └── libmprpc.a
├── src/                  # 源代码目录
│   ├── include/          # 头文件
│   ├── Logger.cc         # 日志系统实现
│   ├── mprpcapplication.cc  # 应用层实现
│   ├── mprpcchannel.cc   # RPC通道实现
│   ├── mprpcconfig.cc    # 配置管理实现
│   ├── mprpccontroller.cc # RPC控制器实现
│   ├── mprpcprovider.cc  # 服务提供者实现
│   ├── rpcheader.pb.cc   # RPC协议头实现
│   ├── rpcheader.proto   # RPC协议头定义
│   └── zookeeperutil.cc  # Zookeeper工具实现
└── test/                 # 单元测试
```

## 📚 学习资源

- **Protocol Buffers官方文档**: https://developers.google.com/protocol-buffers
- **Muduo网络库**: https://github.com/chenshuo/muduo
- **CMake教程**: https://cmake.org/cmake/help/latest/guide/tutorial/
- **RPC原理**: 《分布式系统：概念与设计》

