"""Unit tests for the Mprpc wire protocol encode/decode."""

import struct
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mprpc.protocol import encode_request, decode_header, recv_exact


class TestEncodeRequest(unittest.TestCase):
    """Test that encode_request produces correct wire format."""

    def test_basic_encode(self):
        """Test basic encoding of a request."""
        req_data = b"\x08\x01\x12\x08\x74\x65\x73\x74\x5f\x75\x73\x65\x72\x1a\x08\x74\x65\x73\x74\x5f\x70\x77\x64"
        wire = encode_request("UserServiceRpc", "Login", req_data)

        # First 4 bytes: header_size (little-endian)
        header_size = struct.unpack("<I", wire[:4])[0]
        self.assertGreater(header_size, 0)
        self.assertLess(header_size, 1024)

        # Rest: header + args
        self.assertEqual(len(wire), 4 + header_size + len(req_data))

    def test_header_contains_service_method(self):
        """Test that encoded header can be decoded back."""
        wire = encode_request("UserServiceRpc", "Login", b"\x08\x01", trace_id="test-123")

        header_size = struct.unpack("<I", wire[:4])[0]
        header_bytes = wire[4:4 + header_size]
        header = decode_header(header_bytes)

        self.assertEqual(header["service_name"], "UserServiceRpc")
        self.assertEqual(header["method_name"], "Login")
        self.assertEqual(header["args_size"], 2)
        self.assertEqual(header["version"], 1)
        self.assertEqual(header["trace_id"], "test-123")

    def test_empty_request_data(self):
        """Test encoding with empty request data."""
        wire = encode_request("Svc", "Method", b"")
        header_size = struct.unpack("<I", wire[:4])[0]
        header_bytes = wire[4:4 + header_size]
        header = decode_header(header_bytes)

        self.assertEqual(header["args_size"], 0)

    def test_large_request_data(self):
        """Test encoding with large request data."""
        big_data = b"\x42" * 100000
        wire = encode_request("BigSvc", "BigMethod", big_data)
        header_size = struct.unpack("<I", wire[:4])[0]
        header_bytes = wire[4:4 + header_size]
        header = decode_header(header_bytes)

        self.assertEqual(header["args_size"], 100000)
        self.assertEqual(len(wire), 4 + header_size + 100000)


class TestDecodeHeader(unittest.TestCase):
    """Test RpcHeader decoding."""

    def test_decode_all_fields(self):
        """Test decoding all header fields."""
        from mprpc.rpcheader_pb2 import RpcHeader

        h = RpcHeader()
        h.service_name = "FriendServiceRpc"
        h.method_name = "GetFriendlist"
        h.args_size = 1234
        h.version = 1
        h.trace_id = "abc-123"
        data = h.SerializeToString()

        result = decode_header(data)
        self.assertEqual(result["service_name"], "FriendServiceRpc")
        self.assertEqual(result["method_name"], "GetFriendlist")
        self.assertEqual(result["args_size"], 1234)
        self.assertEqual(result["version"], 1)
        self.assertEqual(result["trace_id"], "abc-123")

    def test_decode_minimal(self):
        """Test decoding minimal header (no trace_id)."""
        from mprpc.rpcheader_pb2 import RpcHeader

        h = RpcHeader()
        h.service_name = "S"
        h.method_name = "M"
        h.args_size = 0
        data = h.SerializeToString()

        result = decode_header(data)
        self.assertEqual(result["service_name"], "S")
        self.assertEqual(result["method_name"], "M")
        self.assertEqual(result["trace_id"], "")


class TestRecvExact(unittest.TestCase):
    """Test recv_exact helper with mock sockets."""

    def test_recv_exact(self):
        """Test receiving exact number of bytes."""
        import io

        class MockSocket:
            def __init__(self, data):
                self._data = data
                self._pos = 0

            def recv(self, n):
                end = min(self._pos + n, len(self._data))
                chunk = self._data[self._pos:end]
                self._pos = end
                return chunk

        sock = MockSocket(b"hello world")
        result = recv_exact(sock, 5)
        self.assertEqual(result, b"hello")

        result = recv_exact(sock, 6)
        self.assertEqual(result, b" world")


class TestWireCompatibility(unittest.TestCase):
    """Test that our encoding matches the C++ expected format."""

    def test_wire_byte_layout(self):
        """Verify exact byte layout matches C++."""
        from mprpc.rpcheader_pb2 import RpcHeader

        # Simulate what C++ does
        req_data = b"\x08\x01\x1a\x04\x74\x65\x73\x74"

        header = RpcHeader()
        header.service_name = "UserServiceRpc"
        header.method_name = "Login"
        header.args_size = len(req_data)
        header.version = 1
        header_bytes = header.SerializeToString()

        # Our encoding
        wire = encode_request("UserServiceRpc", "Login", req_data)

        # Verify structure
        wire_header_size = struct.unpack("<I", wire[:4])[0]
        self.assertEqual(wire_header_size, len(header_bytes))
        self.assertEqual(wire[4:4 + wire_header_size], header_bytes)
        self.assertEqual(wire[4 + wire_header_size:], req_data)


if __name__ == "__main__":
    unittest.main()
