"""Basic tests for the Mprpc Python SDK."""

import struct
import sys
import os
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))


class TestProtobufEncoding:
    """Test the manual protobuf header encoding/decoding."""

    def test_encode_decode_header_simple(self):
        from mprpc.channel import _encode_rpc_header, _decode_rpc_header

        header = _encode_rpc_header("UserService", "Login", 128, version=1, trace_id="abc123")
        decoded = _decode_rpc_header(header)
        assert decoded["service_name"] == "UserService"
        assert decoded["method_name"] == "Login"
        assert decoded["args_size"] == 128
        assert decoded["version"] == 1
        assert decoded["trace_id"] == "abc123"

    def test_encode_header_empty_trace(self):
        from mprpc.channel import _encode_rpc_header, _decode_rpc_header

        header = _encode_rpc_header("Svc", "Method", 0, version=1)
        decoded = _decode_rpc_header(header)
        assert decoded["service_name"] == "Svc"
        assert decoded["method_name"] == "Method"
        assert decoded["args_size"] == 0
        assert decoded["trace_id"] == ""

    def test_wire_format(self):
        from mprpc.channel import _encode_rpc_header, _decode_rpc_header

        service = "mprpc.UserRpcService"
        method = "Login"
        args = b"\x0a\x04test\x12\x06\x61\x62\x63"
        header = _encode_rpc_header(service, method, len(args), version=1, trace_id="trace1")
        header_size = len(header)

        wire = struct.pack("<I", header_size) + header + args

        parsed_size = struct.unpack("<I", wire[:4])[0]
        assert parsed_size == header_size
        parsed_header = _decode_rpc_header(wire[4:4 + header_size])
        assert parsed_header["service_name"] == service
        assert parsed_header["method_name"] == method
        assert parsed_header["args_size"] == len(args)
        parsed_args = wire[4 + header_size:]
        assert parsed_args == args

    def test_large_varint(self):
        from mprpc.channel import _encode_rpc_header, _decode_rpc_header

        header = _encode_rpc_header("S", "M", 100000, version=1)
        decoded = _decode_rpc_header(header)
        assert decoded["args_size"] == 100000

    def test_unicode_service_name(self):
        from mprpc.channel import _encode_rpc_header, _decode_rpc_header

        header = _encode_rpc_header("用户服务", "登录", 10, version=1, trace_id="中文追踪")
        decoded = _decode_rpc_header(header)
        assert decoded["service_name"] == "用户服务"
        assert decoded["method_name"] == "登录"
        assert decoded["trace_id"] == "中文追踪"


class TestMetrics:
    """Test the metrics collector."""

    def test_record_call(self):
        from mprpc.metrics import MetricsCollector

        mc = MetricsCollector()
        mc.record_call("UserService", "Login", 1.5, error=False)
        mc.record_call("UserService", "Login", 2.5, error=False)
        mc.record_call("UserService", "Login", 3.0, error=True)

        m = mc.get_metrics()
        assert "UserService/Login" in m
        assert m["UserService/Login"]["call_count"] == 3
        assert m["UserService/Login"]["error_count"] == 1
        assert abs(m["UserService/Login"]["avg_latency_ms"] - 2.333) < 0.01

    def test_prometheus_export(self):
        from mprpc.metrics import MetricsCollector

        mc = MetricsCollector()
        mc.record_call("Svc", "M1", 10.0)
        mc.record_call("Svc", "M1", 20.0)
        mc.record_blocked("Svc", "M1")

        output = mc.to_prometheus()
        assert "mprpc_rpc_calls_total" in output
        assert 'service="Svc"' in output
        assert 'method="M1"' in output
        assert "mprpc_rpc_blocked_total" in output

    def test_reset(self):
        from mprpc.metrics import MetricsCollector

        mc = MetricsCollector()
        mc.record_call("A", "B", 1.0)
        mc.reset()
        assert mc.get_metrics() == {}


class TestCoreBindings:
    """Test that C++ bindings load correctly."""

    def test_version(self):
        from mprpc.mprpc_core import __version__
        assert __version__ == "2.0.0"

    def test_rpc_config(self):
        from mprpc.mprpc_core import RpcConfig
        cfg = RpcConfig()
        cfg.set("testkey", "testvalue")
        assert cfg.get("testkey") == "testvalue"

    def test_rpc_controller(self):
        from mprpc.mprpc_core import RpcController
        ctrl = RpcController()
        assert not ctrl.failed()
        ctrl.set_failed("test error")
        assert ctrl.failed()
        assert "test error" in ctrl.error_text()
