"""
Mprpc Python SDK - High-performance C++ RPC framework bindings.

Usage:
    from mprpc import init, RpcChannel, RpcConfig

    # Initialize framework
    init("test.conf")

    # Make RPC calls
    channel = RpcChannel()
    response = channel.call("UserService", "Login", request_bytes)
"""

from .mprpc_core import (
    __version__,
    init,
    get_config,
    RpcConfig,
    RpcController,
    ZKClient,
)
from .channel import RpcChannel, RpcError
from .async_channel import AsyncRpcChannel
from .metrics import MetricsCollector

__all__ = [
    "__version__",
    "init",
    "get_config",
    "RpcConfig",
    "RpcController",
    "ZKClient",
    "RpcChannel",
    "AsyncRpcChannel",
    "RpcError",
    "MetricsCollector",
]
