"""Pure Python RPC channel — the core of the Mprpc Python SDK.

Handles ZK service discovery, TCP connection, wire protocol, retries,
and provides both raw-bytes and protobuf-level calling interfaces.
"""

import logging
import random
import socket
import time
import uuid
from typing import Optional

from .config import RpcConfig
from .controller import RpcController
from .error import RpcError
from .zk_client import ZkClient
from .protocol import encode_request, recv_exact, decode_header, send_request, recv_response
from .metrics import MetricsCollector

logger = logging.getLogger("mprpc.channel")

# Cache TTL for discovered addresses (seconds)
_CACHE_TTL_SEC = 30
# Maximum retry attempts
_MAX_RETRY = 3


class RpcChannel:
    """Mprpc RPC channel — discovers providers via ZK and calls them over TCP.

    Usage with raw bytes:
        channel = RpcChannel(config)
        ctrl = RpcController()
        resp_data = channel.call("UserServiceRpc", "Login", req_bytes, ctrl)
        channel.close()

    Usage with protobuf (recommended):
        channel = RpcChannel(config)
        ctrl = RpcController()
        resp = channel.call_proto(
            "UserServiceRpc", "Login",
            request_msg, LoginResponse(), ctrl
        )
    """

    def __init__(
        self,
        config: Optional[RpcConfig] = None,
        zk_addr: str = "127.0.0.1:2181",
        connect_timeout: float = 1.0,
        recv_timeout: float = 5.0,
    ):
        self._config = config
        self._connect_timeout = connect_timeout
        self._recv_timeout = recv_timeout
        self._direct_addr: Optional[str] = None  # bypass ZK if set
        self._zk = ZkClient(zk_addr)
        self._zk_started = False
        self._metrics = MetricsCollector()
        # Address cache: method_path -> (addr, timestamp)
        self._cache: dict[str, tuple[str, float]] = {}

    def set_direct_address(self, addr: str):
        """Set a direct address (ip:port), bypassing ZK discovery."""
        self._direct_addr = addr

    def _ensure_zk(self):
        """Lazily connect to ZooKeeper."""
        if self._zk_started:
            return
        if not self._zk.start():
            raise RpcError("Failed to connect to ZooKeeper", code=-1)
        self._zk_started = True

    def _discover(self, method_path: str) -> Optional[str]:
        """Discover a provider address for the given method.

        Uses direct address if set, otherwise queries ZK with cache.
        """
        if self._direct_addr:
            return self._direct_addr

        # Check cache
        if method_path in self._cache:
            addr, ts = self._cache[method_path]
            if time.time() - ts < _CACHE_TTL_SEC:
                return addr

        self._ensure_zk()
        addr = self._zk.get_service_addr(method_path)
        if addr:
            self._cache[method_path] = (addr, time.time())
        return addr

    def _invalidate_cache(self, method_path: str):
        """Remove a cached address entry."""
        self._cache.pop(method_path, None)

    def call(
        self,
        service_name: str,
        method_name: str,
        request_data: bytes,
        controller: Optional[RpcController] = None,
        trace_id: str = "",
    ) -> bytes:
        """Make a synchronous RPC call with raw bytes.

        Args:
            service_name: Protobuf service name (e.g. "UserServiceRpc").
            method_name: Protobuf method name (e.g. "Login").
            request_data: Serialized protobuf request bytes.
            controller: Optional RpcController to track errors.
            trace_id: Optional trace ID.

        Returns:
            Serialized protobuf response bytes.

        Raises:
            RpcError: If controller is None and call fails.
        """
        if not trace_id:
            trace_id = f"py-{uuid.uuid4().hex[:16]}"

        method_path = f"/{service_name}/{method_name}"
        last_error = ""

        for attempt in range(_MAX_RETRY):
            # Discover service
            host_data = self._discover(method_path)
            if not host_data:
                last_error = f"service discovery failed for {method_path}"
                self._invalidate_cache(method_path)
                continue

            # Parse ip:port
            try:
                ip, port_str = host_data.split(":")
                port = int(port_str)
            except (ValueError, AttributeError):
                last_error = f"invalid address: {host_data}"
                continue

            # TCP connect + send + recv
            sock = None
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(self._connect_timeout)
                sock.connect((ip, port))

                t0 = time.monotonic()
                send_request(sock, service_name, method_name, request_data,
                             version=1, trace_id=trace_id)
                result = recv_response(sock, timeout_sec=self._recv_timeout)
                latency_ms = (time.monotonic() - t0) * 1000

                resp_data = result["response_data"]
                if not resp_data:
                    last_error = f"server returned empty response from {host_data}"
                    continue

                self._metrics.record_call(service_name, method_name, latency_ms)
                return resp_data

            except (socket.timeout, ConnectionError, OSError) as e:
                last_error = f"network error ({host_data}): {e}"
                self._invalidate_cache(method_path)
                continue
            finally:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass

        # All retries exhausted
        if controller:
            controller.set_failed(last_error)
            return b""
        raise RpcError(last_error, code=-2)

    def call_proto(
        self,
        service_name: str,
        method_name: str,
        request_msg,
        response_msg,
        controller: Optional[RpcController] = None,
        trace_id: str = "",
    ):
        """Make an RPC call using protobuf Message objects.

        Args:
            service_name: Protobuf service name.
            method_name: Protobuf method name.
            request_msg: Protobuf Message to serialize and send.
            response_msg: Protobuf Message to deserialize response into.
            controller: Optional RpcController.
            trace_id: Optional trace ID.

        Returns:
            The filled response_msg, or None on error.
        """
        request_data = request_msg.SerializeToString()
        resp_bytes = self.call(service_name, method_name, request_data,
                               controller=controller, trace_id=trace_id)
        if resp_bytes:
            response_msg.ParseFromString(resp_bytes)
            return response_msg
        return None

    def get_metrics(self) -> MetricsCollector:
        """Return the metrics collector."""
        return self._metrics

    def close(self):
        """Close the channel and release resources."""
        if self._zk:
            self._zk.close()
            self._zk_started = False
