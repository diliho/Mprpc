"""Alert API - Prometheus AlertManager integration and custom alert rules.

Queries AlertManager for active alerts and provides custom rule management.
"""

import httpx
from datetime import datetime
from typing import Optional, List
from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel

from ..config import ALERTMANAGER_URL

router = APIRouter(prefix="/api/v1/alerts", tags=["alerts"])


# ── Schemas ───────────────────────────────────────────────────

class AlertRule(BaseModel):
    id: int = 0
    name: str
    condition: str  # e.g. "qps_drop_50", "error_rate_gt_5", "latency_p99_gt_10"
    threshold: float = 0.0
    duration: str = "5m"
    severity: str = "warning"  # info / warning / critical
    enabled: bool = True
    notify_channels: List[str] = []  # email / dingtalk / webhook
    created_at: str = ""


class ActiveAlert(BaseModel):
    alertname: str
    severity: str
    state: str  # active / suppressed / pending
    summary: str = ""
    description: str = ""
    starts_at: str = ""
    labels: dict = {}
    annotations: dict = {}


class AlertSummary(BaseModel):
    total_active: int = 0
    total_pending: int = 0
    total_suppressed: int = 0
    alerts: List[ActiveAlert] = []


# ── In-memory rule store ─────────────────────────────────────

_rules: List[AlertRule] = []
_next_id = 1

# Default rules
_DEFAULT_RULES = [
    AlertRule(name="HighErrorRate", condition="error_rate_gt_5", threshold=5.0, severity="critical",
              notify_channels=["email"]),
    AlertRule(name="HighLatencyP99", condition="latency_p99_gt_10", threshold=10.0, severity="warning",
              notify_channels=["email"]),
    AlertRule(name="QPSSuddenDrop", condition="qps_drop_50", threshold=50.0, severity="warning",
              notify_channels=["email"]),
    AlertRule(name="NodeDown", condition="node_down", threshold=1.0, severity="critical",
              notify_channels=["email", "dingtalk"]),
]


def _init_default_rules():
    global _next_id
    if not _rules:
        for r in _DEFAULT_RULES:
            r.id = _next_id
            r.created_at = datetime.utcnow().isoformat()
            _rules.append(r)
            _next_id += 1


# ── AlertManager query ───────────────────────────────────────

async def _query_alertmanager(endpoint: str) -> dict:
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            resp = await client.get(f"{ALERTMANAGER_URL}{endpoint}")
            resp.raise_for_status()
            return resp.json()
    except Exception:
        return []


# ── API Endpoints ─────────────────────────────────────────────

@router.get("/active", response_model=AlertSummary)
async def get_active_alerts():
    """Get currently active alerts from AlertManager."""
    raw = await _query_alertmanager("/api/v2/alerts")

    alerts = []
    active = 0
    pending = 0
    suppressed = 0

    if isinstance(raw, list):
        for a in raw:
            labels = a.get("labels", {})
            annotations = a.get("annotations", {})
            state = a.get("status", {}).get("state", "active")
            severity = labels.get("severity", "unknown")

            alerts.append(ActiveAlert(
                alertname=labels.get("alertname", "unknown"),
                severity=severity,
                state=state,
                summary=annotations.get("summary", ""),
                description=annotations.get("description", ""),
                starts_at=a.get("startsAt", ""),
                labels=labels,
                annotations=annotations,
            ))

            if state == "active":
                active += 1
            elif state == "pending":
                pending += 1
            elif state == "suppressed":
                suppressed += 1

    return AlertSummary(
        total_active=active,
        total_pending=pending,
        total_suppressed=suppressed,
        alerts=alerts,
    )


@router.get("/rules", response_model=List[AlertRule])
def list_alert_rules():
    """List all alert rules."""
    _init_default_rules()
    return _rules


@router.post("/rules", response_model=AlertRule, status_code=201)
def create_alert_rule(rule: AlertRule):
    """Create a new alert rule."""
    global _next_id
    _init_default_rules()
    rule.id = _next_id
    rule.created_at = datetime.utcnow().isoformat()
    _rules.append(rule)
    _next_id += 1
    return rule


@router.put("/rules/{rule_id}", response_model=AlertRule)
def update_alert_rule(rule_id: int, update: AlertRule):
    """Update an alert rule."""
    _init_default_rules()
    for i, r in enumerate(_rules):
        if r.id == rule_id:
            update.id = rule_id
            update.created_at = r.created_at
            _rules[i] = update
            return update
    raise HTTPException(404, "Rule not found")


@router.delete("/rules/{rule_id}")
def delete_alert_rule(rule_id: int):
    """Delete an alert rule."""
    _init_default_rules()
    for i, r in enumerate(_rules):
        if r.id == rule_id:
            _rules.pop(i)
            return {"detail": "deleted"}
    raise HTTPException(404, "Rule not found")


@router.get("/manager/status")
async def alertmanager_status():
    """Check if AlertManager is reachable."""
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            resp = await client.get(f"{ALERTMANAGER_URL}/-/healthy")
            if resp.status_code == 200:
                return {"status": "connected", "url": ALERTMANAGER_URL}
    except Exception:
        pass
    return {"status": "disconnected", "url": ALERTMANAGER_URL}
