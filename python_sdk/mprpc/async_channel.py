"""Async RPC channel using asyncio for non-blocking calls."""

import asyncio
import struct
import uuid
from typing import Optional, Tuple

from .channel import (
    _encode_rpc_header,
    _decode_rpc_header,
    RpcError,
)


class AsyncRpcChannel:
    """Async RPC channel using asyncio sockets.

    Usage:
        import asyncio
        from mprpc import init
        from mprpc.async_channel import AsyncRpcChannel

        init("test.conf")

        async def main():
            channel = AsyncRpcChannel()
            response = await channel.call("UserService", "Login", request_bytes)
            await channel.close()

        asyncio.run(main())
    """

    def __init__(
        self,
        connect_timeout_sec: float = 3.0,
        recv_timeout_sec: float = 5.0,
        max_retries: int = 3,
    ):
        self._connect_timeout = connect_timeout_sec
        self._recv_timeout = recv_timeout_sec
        self._max_retries = max_retries
        self._zk = None
        self._zk_started = False

    def _ensure_zk(self):
        if not self._zk_started:
            from .mprpc_core import ZKClient
            self._zk = ZKClient()
            if not self._zk.start():
                raise RpcError(-1, "Failed to connect to ZooKeeper")
            self._zk_started = True

    async def call(
        self,
        service_name: str,
        method_name: str,
        request_data: bytes,
        trace_id: str = "",
        timeout_sec: Optional[float] = None,
    ) -> bytes:
        """Make an async RPC call.

        Returns:
            Serialized protobuf response bytes.

        Raises:
            RpcError: On RPC call failure.
        """
        self._ensure_zk()

        if not trace_id:
            trace_id = f"py-aio-{uuid.uuid4().hex[:16]}"

        method_path = f"/{service_name}/{method_name}"
        last_error = ""

        for attempt in range(self._max_retries):
            host_data = self._zk.get_data(method_path)
            if not host_data:
                last_error = f"service discovery failed for {method_path}"
                continue

            try:
                ip, port_str = host_data.split(":")
                port = int(port_str)
            except (ValueError, AttributeError):
                last_error = f"invalid address: {host_data}"
                continue

            args_size = len(request_data)
            header_bytes = _encode_rpc_header(
                service_name, method_name, args_size, version=1, trace_id=trace_id
            )
            header_size = len(header_bytes)
            wire_request = struct.pack("<I", header_size) + header_bytes + request_data

            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(ip, port),
                    timeout=self._connect_timeout,
                )
            except (asyncio.TimeoutError, OSError) as e:
                last_error = f"connect failed: {e}"
                continue

            try:
                writer.write(wire_request)
                await writer.drain()

                resp_len_buf = await asyncio.wait_for(
                    reader.readexactly(4), timeout=timeout_sec or self._recv_timeout
                )
                resp_header_size = struct.unpack("<I", resp_len_buf)[0]

                if resp_header_size > 1024 * 1024:
                    last_error = f"response header too large: {resp_header_size}"
                    continue

                resp_header_bytes = await asyncio.wait_for(
                    reader.readexactly(resp_header_size),
                    timeout=timeout_sec or self._recv_timeout,
                )
                resp_header = _decode_rpc_header(resp_header_bytes)

                resp_args_size = resp_header["args_size"]
                if resp_args_size == 0:
                    last_error = "server returned empty response"
                    continue

                resp_args = await asyncio.wait_for(
                    reader.readexactly(resp_args_size),
                    timeout=timeout_sec or self._recv_timeout,
                )
                return resp_args

            except (asyncio.TimeoutError, ConnectionError) as e:
                last_error = f"network error: {e}"
                continue
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except Exception:
                    pass

        raise RpcError(-2, f"All {self._max_retries} retries failed: {last_error}")

    async def close(self):
        pass
