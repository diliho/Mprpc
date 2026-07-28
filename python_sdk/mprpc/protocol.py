"""Wire protocol encode/decode for the Mprpc RPC framework.

Binary format (little-endian):
    Request:  [4B header_size] [RpcHeader protobuf] [args protobuf bytes]
    Response: [4B header_size] [RpcHeader protobuf] [args protobuf bytes]

RpcHeader fields:
    string service_name = 1;
    string method_name  = 2;
    uint32 args_size    = 3;
    uint32 version      = 4;
    string trace_id     = 5;
"""

import struct

from .rpcheader_pb2 import RpcHeader


def encode_request(
    service_name: str,
    method_name: str,
    request_data: bytes,
    version: int = 1,
    trace_id: str = "",
) -> bytes:
    """Encode an RPC request into wire format.

    Returns:
        Complete wire bytes ready to send over TCP.
    """
    header = RpcHeader()
    header.service_name = service_name
    header.method_name = method_name
    header.args_size = len(request_data)
    header.version = version
    if trace_id:
        header.trace_id = trace_id

    header_bytes = header.SerializeToString()
    header_size = len(header_bytes)

    # [4B header_size LE][header_bytes][args_bytes]
    return struct.pack("<I", header_size) + header_bytes + request_data


def decode_header(data: bytes) -> dict:
    """Decode an RpcHeader from raw bytes.

    Returns:
        dict with keys: service_name, method_name, args_size, version, trace_id
    """
    header = RpcHeader()
    header.ParseFromString(data)
    return {
        "service_name": header.service_name,
        "method_name": header.method_name,
        "args_size": header.args_size,
        "version": header.version,
        "trace_id": header.trace_id,
    }


def recv_exact(sock, n: int) -> bytes:
    """Receive exactly n bytes from a socket.

    Raises:
        ConnectionError: If the connection is closed before all bytes received.
    """
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed by server")
        buf.extend(chunk)
    return bytes(buf)


def send_request(sock, service_name: str, method_name: str,
                 request_data: bytes, version: int = 1, trace_id: str = ""):
    """Encode and send an RPC request over a socket."""
    wire = encode_request(service_name, method_name, request_data, version, trace_id)
    sock.sendall(wire)


def recv_response(sock, timeout_sec: float = 5.0) -> dict:
    """Receive and decode an RPC response from a socket.

    Args:
        sock: Connected TCP socket.
        timeout_sec: Receive timeout in seconds.

    Returns:
        dict with keys: header (dict), response_data (bytes)

    Raises:
        ConnectionError: On connection issues.
        TimeoutError: On receive timeout.
        ValueError: On invalid response.
    """
    import socket as _socket

    old_timeout = sock.gettimeout()
    sock.settimeout(timeout_sec)
    try:
        # Step 1: Read 4-byte header length
        len_buf = recv_exact(sock, 4)
        resp_header_size = struct.unpack("<I", len_buf)[0]

        if resp_header_size > 1024 * 1024:
            raise ValueError(f"response header too large: {resp_header_size}")

        # Step 2: Read header bytes
        resp_header_bytes = recv_exact(sock, resp_header_size)
        header = decode_header(resp_header_bytes)

        # Step 3: Read args bytes
        resp_args_size = header["args_size"]
        if resp_args_size == 0:
            return {"header": header, "response_data": b""}

        response_data = recv_exact(sock, resp_args_size)
        return {"header": header, "response_data": response_data}
    finally:
        sock.settimeout(old_timeout)
