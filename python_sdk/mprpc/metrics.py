"""Python-side metrics collector for RPC calls.

Collects call count, latency, error rate per service/method,
and can export to Prometheus text format.
"""

import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Optional


@dataclass
class MethodMetrics:
    """Metrics for a single service/method pair."""
    call_count: int = 0
    error_count: int = 0
    total_latency_ms: float = 0.0
    max_latency_ms: float = 0.0
    min_latency_ms: float = float("inf")
    blocked_count: int = 0

    @property
    def avg_latency_ms(self) -> float:
        if self.call_count == 0:
            return 0.0
        return self.total_latency_ms / self.call_count

    @property
    def error_rate(self) -> float:
        if self.call_count == 0:
            return 0.0
        return self.error_count / self.call_count


class MetricsCollector:
    """Collects and exports RPC metrics in Prometheus text format.

    Usage:
        from mprpc.metrics import MetricsCollector

        mc = MetricsCollector()
        start = time.monotonic()
        # ... do RPC call ...
        elapsed_ms = (time.monotonic() - start) * 1000
        mc.record_call("UserService", "Login", elapsed_ms, error=False)
        print(mc.to_prometheus())
    """

    def __init__(self):
        self._metrics: Dict[str, MethodMetrics] = defaultdict(MethodMetrics)
        self._start_time = time.time()

    def record_call(
        self,
        service: str,
        method: str,
        latency_ms: float,
        error: bool = False,
    ):
        """Record a single RPC call."""
        key = f"{service}/{method}"
        m = self._metrics[key]
        m.call_count += 1
        m.total_latency_ms += latency_ms
        m.max_latency_ms = max(m.max_latency_ms, latency_ms)
        m.min_latency_ms = min(m.min_latency_ms, latency_ms)
        if error:
            m.error_count += 1

    def record_blocked(self, service: str, method: str):
        """Record a rate-limited call."""
        key = f"{service}/{method}"
        self._metrics[key].blocked_count += 1

    def get_metrics(self) -> dict:
        """Return metrics as a dict."""
        result = {}
        for key, m in self._metrics.items():
            result[key] = {
                "call_count": m.call_count,
                "error_count": m.error_count,
                "avg_latency_ms": round(m.avg_latency_ms, 3),
                "max_latency_ms": round(m.max_latency_ms, 3),
                "min_latency_ms": round(m.min_latency_ms, 3) if m.min_latency_ms != float("inf") else 0.0,
                "error_rate": round(m.error_rate, 4),
                "blocked_count": m.blocked_count,
            }
        return result

    def to_prometheus(self) -> str:
        """Export metrics in Prometheus text exposition format."""
        lines = []
        lines.append("# HELP mprpc_rpc_calls_total Total RPC calls")
        lines.append("# TYPE mprpc_rpc_calls_total counter")
        for key, m in self._metrics.items():
            service, method = key.rsplit("/", 1) if "/" in key else (key, "")
            lines.append(
                f'mprpc_rpc_calls_total{{service="{service}",method="{method}"}} {m.call_count}'
            )

        lines.append("# HELP mprpc_rpc_errors_total Total RPC errors")
        lines.append("# TYPE mprpc_rpc_errors_total counter")
        for key, m in self._metrics.items():
            service, method = key.rsplit("/", 1) if "/" in key else (key, "")
            lines.append(
                f'mprpc_rpc_errors_total{{service="{service}",method="{method}"}} {m.error_count}'
            )

        lines.append("# HELP mprpc_rpc_latency_ms_avg Average RPC latency in ms")
        lines.append("# TYPE mprpc_rpc_latency_ms_avg gauge")
        for key, m in self._metrics.items():
            service, method = key.rsplit("/", 1) if "/" in key else (key, "")
            lines.append(
                f'mprpc_rpc_latency_ms_avg{{service="{service}",method="{method}"}} {m.avg_latency_ms}'
            )

        lines.append("# HELP mprpc_rpc_blocked_total Total blocked calls")
        lines.append("# TYPE mprpc_rpc_blocked_total counter")
        for key, m in self._metrics.items():
            service, method = key.rsplit("/", 1) if "/" in key else (key, "")
            lines.append(
                f'mprpc_rpc_blocked_total{{service="{service}",method="{method}"}} {m.blocked_count}'
            )

        return "\n".join(lines) + "\n"

    def reset(self):
        """Reset all metrics."""
        self._metrics.clear()
        self._start_time = time.time()
