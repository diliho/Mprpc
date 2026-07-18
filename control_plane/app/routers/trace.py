"""Trace API - lightweight distributed tracing.

Collects trace spans from structured logs emitted by C++ Provider/Consumer.
Format: [TRACE] trace_id=xxx service=xxx method=xxx phase=xxx elapsed=xxxms
"""

import os
import re
import glob
from datetime import datetime
from typing import Optional, List
from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel

from ..config import TRACE_LOG_DIR

router = APIRouter(prefix="/api/v1/trace", tags=["trace"])


# ── Schemas ───────────────────────────────────────────────────

class TraceSpan(BaseModel):
    phase: str
    service: str = ""
    method: str = ""
    elapsed_ms: float = 0.0
    timestamp: str = ""
    node_ip: str = ""
    detail: str = ""


class TraceDetail(BaseModel):
    trace_id: str
    spans: List[TraceSpan] = []
    total_elapsed_ms: float = 0.0
    start_time: str = ""
    status: str = "success"


class TraceListItem(BaseModel):
    trace_id: str
    service: str
    method: str
    total_elapsed_ms: float
    status: str
    timestamp: str


# ── Log parsing ───────────────────────────────────────────────

_TRACE_PATTERN = re.compile(
    r"\[TRACE\]\s+"
    r"trace_id=(?P<trace_id>\S+)\s+"
    r"service=(?P<service>\S+)\s+"
    r"method=(?P<method>\S+)\s+"
    r"phase=(?P<phase>\S+)\s+"
    r"elapsed=(?P<elapsed>[\d.]+)ms"
)


def _parse_trace_logs(log_dir: str) -> dict:
    """Parse trace spans from log files in the given directory."""
    traces = {}
    log_files = glob.glob(os.path.join(log_dir, "*.log")) + glob.glob(os.path.join(log_dir, "*.txt"))

    for log_file in log_files:
        try:
            with open(log_file, "r", errors="ignore") as f:
                for line in f:
                    m = _TRACE_PATTERN.search(line)
                    if not m:
                        continue
                    tid = m.group("trace_id")
                    span = TraceSpan(
                        phase=m.group("phase"),
                        service=m.group("service"),
                        method=m.group("method"),
                        elapsed_ms=float(m.group("elapsed")),
                    )
                    if tid not in traces:
                        traces[tid] = {"spans": [], "service": span.service, "method": span.method}
                    traces[tid]["spans"].append(span)
        except (OSError, IOError):
            continue

    return traces


# ── In-memory trace store (for demo) ─────────────────────────

_trace_store: dict = {}


def record_trace(trace_id: str, service: str, method: str, phase: str, elapsed_ms: float):
    """Record a trace span (called from metrics collection or log parsing)."""
    if trace_id not in _trace_store:
        _trace_store[trace_id] = {"spans": [], "service": service, "method": method}
    _trace_store[trace_id]["spans"].append(TraceSpan(
        phase=phase,
        service=service,
        method=method,
        elapsed_ms=elapsed_ms,
        timestamp=datetime.utcnow().isoformat(),
    ))


# ── API Endpoints ─────────────────────────────────────────────

@router.get("/{trace_id}", response_model=TraceDetail)
async def get_trace(trace_id: str):
    """Get full trace detail by trace_id."""
    # First check in-memory store
    if trace_id in _trace_store:
        data = _trace_store[trace_id]
        spans = data["spans"]
        total = sum(s.elapsed_ms for s in spans)
        return TraceDetail(
            trace_id=trace_id,
            spans=spans,
            total_elapsed_ms=round(total, 3),
            status="success",
        )

    # Then scan log files
    traces = _parse_trace_logs(TRACE_LOG_DIR)
    if trace_id in traces:
        data = traces[trace_id]
        spans = data["spans"]
        total = sum(s.elapsed_ms for s in spans)
        return TraceDetail(
            trace_id=trace_id,
            spans=spans,
            total_elapsed_ms=round(total, 3),
            status="success",
        )

    raise HTTPException(404, f"Trace {trace_id} not found")


@router.get("", response_model=List[TraceListItem])
async def list_traces(
    service: Optional[str] = Query(None),
    limit: int = Query(50, le=200),
):
    """List recent traces."""
    items = []

    # From in-memory store
    for tid, data in _trace_store.items():
        spans = data["spans"]
        total = sum(s.elapsed_ms for s in spans)
        if service and data["service"] != service:
            continue
        items.append(TraceListItem(
            trace_id=tid,
            service=data["service"],
            method=data["method"],
            total_elapsed_ms=round(total, 3),
            status="success",
            timestamp=spans[0].timestamp if spans else "",
        ))

    # From log files
    traces = _parse_trace_logs(TRACE_LOG_DIR)
    for tid, data in traces.items():
        if tid in _trace_store:
            continue
        spans = data["spans"]
        total = sum(s.elapsed_ms for s in spans)
        if service and data["service"] != service:
            continue
        items.append(TraceListItem(
            trace_id=tid,
            service=data["service"],
            method=data["method"],
            total_elapsed_ms=round(total, 3),
            status="success",
            timestamp="",
        ))

    items.sort(key=lambda x: x.timestamp, reverse=True)
    return items[:limit]


@router.delete("")
async def clear_traces():
    """Clear in-memory trace store."""
    _trace_store.clear()
    return {"detail": "cleared"}
