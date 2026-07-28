# Mprpc Python SDK

Pure Python RPC client compatible with C++ Mprpc providers. No native compilation needed.

## Features

- Wire protocol compatible with C++ Mprpc providers (binary, protobuf-based)
- ZooKeeper service discovery via kazoo
- Sync RPC calls with retry and error handling
- Direct-connect mode (bypass ZK)
- Client-side metrics with Prometheus export
- `pip install` with no C++ dependencies

## Install

```bash
pip install -e .
```

## Quick Start

```python
from mprpc import RpcChannel, RpcConfig, RpcController
from user_pb2 import LoginRequest, LoginResponse

config = RpcConfig()
config.load("test.conf")

channel = RpcChannel(config)

# Build request
req = LoginRequest(name=b"test", pwd=b"123456")
resp = LoginResponse()
ctrl = RpcController()

# Call C++ provider via ZK
channel.call_proto("UserServiceRpc", "Login", req, resp, ctrl)

if ctrl.failed():
    print(f"Error: {ctrl.error_text()}")
else:
    print(f"Success: {resp.success}, msg: {resp.result.errormsg.decode()}")
```

## Direct Connect (bypass ZK)

```python
channel = RpcChannel(config)
channel.set_direct_address("127.0.0.1:8001")

resp = channel.call_proto("UserServiceRpc", "Login", req, LoginResponse(), ctrl)
```

## Generate Protobuf Python Code

```bash
protoc --python_out=. user.proto friend.proto
```

## Run Tests

```bash
python -m pytest tests/ -v
```

## Run Example

```bash
python example/call_service.py
```
