"""Pure Python RPC channel implementing the Mprpc wire protocol.

Wire format:
    Request:  [4 bytes header_size (LE)] [RpcHeader proto] [request proto bytes]
    Response: [4 bytes header_size (LE)] [RpcHeader proto] [response proto bytes]
"""

from .config import RpcConfig
from .channel import RpcChannel
from .controller import RpcController
from .error import RpcError
from .zk_client import ZkClient
from .metrics import MetricsCollector

__version__ = "1.0.0"
__all__ = [
    "RpcConfig", "RpcChannel", "RpcController", "RpcError",
    "ZkClient", "MetricsCollector",
]
