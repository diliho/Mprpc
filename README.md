# README.md - MPRPC 分布式网络通信框架

# MPRPC - C++分布式网络通信框架

一个基于muduo网络库和protobuf序列化的RPC通信框架实现，旨在深入理解分布式系统中远程过程调用的核心原理。

## 📋 项目概述

本项目通过自研RPC框架，深入探索分布式系统中服务通信的核心机制：

- **网络通信层**：基于muduo的高性能TCP网络库
- **序列化层**：使用protobuf进行高效数据序列化
- **RPC协议层**：自定义RPC通信协议
- **服务治理**：服务注册、发现、负载均衡等基础功能

### 一键环境配置

项目提供了自动化环境配置脚本，一键安装所有依赖：

```bash
# 1. 设置脚本执行权限
chmod +x auto_setup.sh autobuild.sh check_env.sh

# 2. 运行环境配置脚本
./auto_setup.sh

# 3. 使环境变量生效
source ~/.bashrc
# 或者重新打开终端
```

### 环境验证

配置完成后，使用验证脚本检查环境状态：

```bash
./check_env.sh
```

预期输出应包含：
```
✓ protoc: /usr/local/bin/protoc
✓ 版本: libprotoc 3.20.3
✓ muduo路径: /tmp/muduo/build/release-install
✓ g++: g++ (Ubuntu 11.4.0) ...
```

## 📁 项目结构

```
MPRPC/
├── auto_setup.sh          # 一键环境配置脚本
├── autobuild.sh          # 项目构建脚本
├── check_env.sh          # 环境验证脚本
├── CMakeLists.txt        # CMake构建配置
├── README.md             # 项目说明文档
├── proto/                # Protocol Buffers定义文件
│   └── mprpc.proto       # RPC协议定义示例
├── src/                  # 源代码目录
│   ├── include/          # 头文件目录
│   │   └── mprpc.common.h # 公共头文件
│   ├── common/           # 公共组件实现
│   ├── server/           # 服务器端实现
│   └── client/           # 客户端实现
├── example/              # 使用示例
├── test/                 # 单元测试
├── lib/                  # 生成的静态/动态库
└── bin/                  # 生成的可执行文件
```

## 📚 学习资源

- **Protocol Buffers官方文档**: https://developers.google.com/protocol-buffers
- **Muduo网络库**: https://github.com/chenshuo/muduo
- **CMake教程**: https://cmake.org/cmake/help/latest/guide/tutorial/
- **RPC原理**: 《分布式系统：概念与设计》

