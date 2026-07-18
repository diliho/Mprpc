"""Pure Python RPC channel implementing the Mprpc wire protocol.

Wire format:
    Request:  [4 bytes header_size (LE)] [RpcHeader proto] [request proto bytes]
    Response: [4 bytes header_size (LE)] [RpcHeader proto] [response proto bytes]

RpcHeader proto fields:
    string service_name = 1;
    string method_name  = 2;
    uint32 args_size    = 3;
    uint32 version      = 4;
    string trace_id     = 5;
"""

import socket
import struct
import time
import uuid
from typing import Optional, Tuple

from .mprpc_core import init as _cpp_init, get_config as _get_config, ZKClient


class RpcError(Exception):
    """RPC call error."""
    def __init__(self, code: int = 0, message: str = ""):
        self.code = code
        self.message = message
        super().__init__(f"RpcError({code}): {message}")


def _encode_varint(value: int) -> bytes:
    """Encode an integer as a protobuf varint."""
    result = bytearray()
    while value > 0x7F:
        result.append((value & 0x7F) | 0x80)
        value >>= 7
    result.append(value & 0x7F)
    return bytes(result)


def _decode_varint(data: bytes, offset: int) -> Tuple[int, int]:
    """Decode a protobuf varint, return (value, new_offset)."""
    result = 0
    shift = 0
    while offset < len(data):
        b = data[offset]
        result |= (b & 0x7F) << shift
        offset += 1
        if (b & 0x80) == 0:
            break
        shift += 7
    return result, offset


def _encode_rpc_header(
    service_name: str,
    method_name: str,
    args_size: int,
    version: int = 1,
    trace_id: str = "",
) -> bytes:
    """Manually encode an RpcHeader protobuf message."""
    buf = bytearray()

    # field 1: string service_name (tag = 0x0a)
    sn_bytes = service_name.encode("utf-8")
    buf.append(0x0A)
    buf.extend(_encode_varint(len(sn_bytes)))
    buf.extend(sn_bytes)

    # field 2: string method_name (tag = 0x12)
    mn_bytes = method_name.encode("utf-8")
    buf.append(0x12)
    buf.extend(_encode_varint(len(mn_bytes)))
    buf.extend(mn_bytes)

    # field 3: uint32 args_size (tag = 0x18)
    buf.append(0x18)
    buf.extend(_encode_varint(args_size))

    # field 4: uint32 version (tag = 0x20)
    buf.append(0x20)
    buf.extend(_encode_varint(version))

    # field 5: string trace_id (tag = 0x2a)
    if trace_id:
        tid_bytes = trace_id.encode("utf-8")
        buf.append(0x2A)
        buf.extend(_encode_varint(len(tid_bytes)))
        buf.extend(tid_bytes)

    return bytes(buf)


def _decode_rpc_header(data: bytes) -> dict:
    """Decode an RpcHeader protobuf message from raw bytes."""
    result = {"service_name": "", "method_name": "", "args_size": 0, "version": 0, "trace_id": ""}
    offset = 0
    while offset < len(data):
        tag, offset = _decode_varint(data, offset)
        field_num = tag >> 3
        wire_type = tag & 0x07

        if wire_type == 2:  # length-delimited (string, bytes, embedded)
            length, offset = _decode_varint(data, offset)
            value = data[offset:offset + length]
            offset += length
            if field_num == 1:
                result["service_name"] = value.decode("utf-8")
            elif field_num == 2:
                result["method_name"] = value.decode("utf-8")
            elif field_num == 5:
                result["trace_id"] = value.decode("utf-8")
        elif wire_type == 0:  # varint
            value, offset = _decode_varint(data, offset)
            if field_num == 3:
                result["args_size"] = value
            elif field_num == 4:
                result["version"] = value
        else:
            break  # unknown wire type, stop parsing

    return result


class RpcChannel:
    """Pure Python RPC channel implementing the Mprpc wire protocol.

    Usage:
        from mprpc import init
        from mprpc.channel import RpcChannel

        init("test.conf")
        channel = RpcChannel()
        response_bytes = channel.call("UserService", "Login", request_bytes)
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
        self._zk = ZKClient()
        self._zk_started = False

    def _ensure_zk(self):
        if not self._zk_started:
            if not self._zk.start():
                raise RpcError(-1, "Failed to connect to ZooKeeper")
            self._zk_started = True

    def call(
        self,
        service_name: str,
        method_name: str,
        request_data: bytes,
        trace_id: str = "",
        timeout_sec: Optional[float] = None,
    ) -> bytes:
        """Make a synchronous RPC call.

        Args:
            service_name: The protobuf service name (e.g. "mprpc.UserRpcService").
            method_name: The protobuf method name (e.g. "Login").
            request_data: Serialized protobuf request bytes.
            trace_id: Optional trace ID for distributed tracing.
            timeout_sec: Optional per-call timeout override.

        Returns:
            Serialized protobuf response bytes.

        Raises:
            RpcError: On RPC call failure.
        """
        self._ensure_zk()

        if not trace_id:
            trace_id = f"py-{uuid.uuid4().hex[:16]}"

        method_path = f"/{service_name}/{method_name}"
        last_error = ""

        for attempt in range(self._max_retries):
            # Discover service
            host_data = self._zk.get_data(method_path)
            if not host_data:
                last_error = f"service discovery failed for {method_path}"
                continue

            # host_data format: "ip:port"
            try:
                ip, port_str = host_data.split(":")
                port = int(port_str)
            except (ValueError, AttributeError):
                last_error = f"invalid address: {host_data}"
                continue

            # Build request
            args_size = len(request_data)
            header_bytes = _encode_rpc_header(
                service_name, method_name, args_size, version=1, trace_id=trace_id
            )
            header_size = len(header_bytes)

            # Wire: [4 bytes header_size][header][args]
            wire_request = struct.pack("<I", header_size) + header_bytes + request_data

            # Send and receive
            sock = None
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(self._connect_timeout)
                sock.connect((ip, port))

                sock.settimeout(timeout_sec or self._recv_timeout)
                sock.sendall(wire_request)

                # Read response header size (4 bytes)
                resp_len_buf = self._recv_exact(sock, 4)
                resp_header_size = struct.unpack("<I", resp_len_buf)[0]

                if resp_header_size > 1024 * 1024:
                    last_error = f"response header too large: {resp_header_size}"
                    continue

                # Read response header
                resp_header_bytes = self._recv_exact(sock, resp_header_size)
                resp_header = _decode_rpc_header(resp_header_bytes)

                resp_args_size = resp_header["args_size"]
                if resp_args_size == 0:
                    last_error = "server returned empty response"
                    continue

                # Read response args
                resp_args = self._recv_exact(sock, resp_args_size)
                return resp_args

            except (socket.timeout, OSError) as e:
                last_error = f"network error: {e}"
                continue
            finally:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass

        raise RpcError(-2, f"All {self._max_retries} retries failed: {last_error}")

    @staticmethod
    def _recv_exact(sock: socket.socket, n: int) -> bytes:
        """Receive exactly n bytes from socket."""
        buf = bytearray()
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("connection closed by server")
            buf.extend(chunk)
        return bytes(buf)

    def close(self):
        """Close the channel and release resources."""
        pass
