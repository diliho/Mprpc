#!/usr/bin/env python3
"""Example: Python SDK calling C++ Mprpc Providers.

This script demonstrates calling the UserServiceRpc.Login and
UserServiceRpc.Register methods on running C++ providers via
ZooKeeper service discovery.

Prerequisites:
    - C++ providers running (Docker Compose or native)
    - ZooKeeper running on localhost:2181
    - protoc generated: user_pb2.py (run: protoc --python_out=. user.proto)
"""

import sys
import os
import time

# Add parent dir so we can import mprpc and example protos
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mprpc import RpcChannel, RpcConfig, RpcController
from example.user_pb2 import LoginRequest, LoginResponse, RegisterRequest, RegisterResponse


def test_login(config: RpcConfig, zk_addr: str = "127.0.0.1:2181"):
    """Test Login RPC call."""
    print("=" * 60)
    print("  Test: UserServiceRpc.Login")
    print("=" * 60)

    channel = RpcChannel(config, zk_addr=zk_addr)
    ctrl = RpcController()

    req = LoginRequest()
    req.name = b"test_user"
    req.pwd = b"test_pwd"

    t0 = time.monotonic()
    resp = channel.call_proto("UserServiceRpc", "Login", req, LoginResponse(), ctrl)
    latency_ms = (time.monotonic() - t0) * 1000

    if ctrl.failed():
        print(f"  FAILED: {ctrl.error_text()}")
    else:
        print(f"  Success: {resp.success}")
        print(f"  Message: {resp.result.errormsg.decode()}")
        print(f"  Latency: {latency_ms:.2f}ms")

    # Test with invalid credentials
    print()
    ctrl.reset()
    req2 = LoginRequest()
    req2.name = b"invalid"
    req2.pwd = b"invalid"
    resp2 = channel.call_proto("UserServiceRpc", "Login", req2, LoginResponse(), ctrl)

    if ctrl.failed():
        print(f"  Invalid login (expected error): {ctrl.error_text()[:80]}")
    else:
        print(f"  Invalid login: success={resp2.success}")

    channel.close()
    return latency_ms


def test_register(config: RpcConfig, zk_addr: str = "127.0.0.1:2181"):
    """Test Register RPC call."""
    print()
    print("=" * 60)
    print("  Test: UserServiceRpc.Register")
    print("=" * 60)

    channel = RpcChannel(config, zk_addr=zk_addr)
    ctrl = RpcController()

    req = RegisterRequest()
    req.id = 42
    req.name = b"new_user"
    req.pwd = b"new_pwd"

    t0 = time.monotonic()
    resp = channel.call_proto("UserServiceRpc", "Register", req, RegisterResponse(), ctrl)
    latency_ms = (time.monotonic() - t0) * 1000

    if ctrl.failed():
        print(f"  FAILED: {ctrl.error_text()}")
    else:
        print(f"  Success: {resp.success}")
        print(f"  Latency: {latency_ms:.2f}ms")

    channel.close()
    return latency_ms


def test_direct_connect(config: RpcConfig, addr: str):
    """Test direct connect mode (bypass ZK)."""
    print()
    print("=" * 60)
    print(f"  Test: Direct Connect to {addr}")
    print("=" * 60)

    channel = RpcChannel(config, zk_addr="unused")
    channel.set_direct_address(addr)
    ctrl = RpcController()

    req = LoginRequest()
    req.name = b"direct_test"
    req.pwd = b"direct_pwd"

    t0 = time.monotonic()
    resp = channel.call_proto("UserServiceRpc", "Login", req, LoginResponse(), ctrl)
    latency_ms = (time.monotonic() - t0) * 1000

    if ctrl.failed():
        print(f"  FAILED: {ctrl.error_text()}")
    else:
        print(f"  Success: {resp.success}")
        print(f"  Message: {resp.result.errormsg.decode()}")
        print(f"  Latency: {latency_ms:.2f}ms")

    channel.close()
    return latency_ms


def test_qps_benchmark(config: RpcConfig, addr: str, duration_sec: int = 3):
    """Simple QPS benchmark against a single provider."""
    print()
    print("=" * 60)
    print(f"  QPS Benchmark: {addr} ({duration_sec}s)")
    print("=" * 60)

    channel = RpcChannel(config, zk_addr="unused")
    channel.set_direct_address(addr)

    req = LoginRequest()
    req.name = b"bench"
    req.pwd = b"bench"

    total_calls = 0
    errors = 0
    latencies = []
    start = time.monotonic()

    while time.monotonic() - start < duration_sec:
        ctrl = RpcController()
        t0 = time.monotonic()
        try:
            resp = channel.call_proto("UserServiceRpc", "Login", req, LoginResponse(), ctrl)
            latency_ms = (time.monotonic() - t0) * 1000
            latencies.append(latency_ms)
            total_calls += 1
            if ctrl.failed():
                errors += 1
        except Exception:
            errors += 1
            total_calls += 1

    elapsed = time.monotonic() - start
    qps = total_calls / elapsed if elapsed > 0 else 0
    latencies.sort()
    avg_lat = sum(latencies) / len(latencies) if latencies else 0
    p99_idx = int(len(latencies) * 0.99) if latencies else 0
    p99_lat = latencies[p99_idx] if latencies else 0

    print(f"  QPS:       {qps:.0f}")
    print(f"  Total:     {total_calls} calls, {errors} errors")
    print(f"  Latency:   avg={avg_lat:.2f}ms  p99={p99_lat:.2f}ms")
    print(f"  Success%:  {100.0 * (1 - errors/total_calls):.1f}%")

    channel.close()
    return qps


if __name__ == "__main__":
    conf_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test.conf")
    zk_addr = "127.0.0.1:2181"

    if len(sys.argv) > 1:
        conf_file = sys.argv[1]
    if len(sys.argv) > 2:
        zk_addr = sys.argv[2]

    config = RpcConfig()
    config.load(conf_file)
    print(f"Config loaded: {conf_file}")
    print(f"ZK address: {zk_addr}")
    print()

    # ZK-based calls
    try:
        test_login(config, zk_addr)
        test_register(config, zk_addr)
    except Exception as e:
        print(f"\nZK-based calls failed: {e}")

    # Direct connect (all 3 providers)
    for port in [8001, 8002, 8003]:
        try:
            test_direct_connect(config, f"127.0.0.1:{port}")
        except Exception as e:
            print(f"\nDirect connect to port {port} failed: {e}")

    # QPS benchmark
    try:
        test_qps_benchmark(config, "127.0.0.1:8001", duration_sec=3)
    except Exception as e:
        print(f"\nQPS benchmark failed: {e}")

    # Metrics
    print()
    print("=" * 60)
    print("  Metrics (Prometheus format)")
    print("=" * 60)
    print("  (no metrics collected in this demo)")
