"""Pydantic schemas for API request/response models."""

from datetime import datetime
from typing import Optional, List, Dict, Any
from pydantic import BaseModel, Field


# ── Node schemas ──────────────────────────────────────────────

class NodeCreate(BaseModel):
    ip: str
    port: int
    hostname: str = ""
    rpc_port: int = 0
    region: str = "default"
    labels: Dict[str, str] = {}


class NodeResponse(BaseModel):
    id: int
    ip: str
    port: int
    hostname: str
    status: str
    rpc_port: int
    region: str
    labels: Dict[str, Any]
    last_heartbeat: Optional[datetime] = None
    created_at: datetime
    updated_at: datetime

    class Config:
        from_attributes = True


class NodeStatusUpdate(BaseModel):
    status: str


# ── Service schemas ───────────────────────────────────────────

class ServiceCreate(BaseModel):
    name: str
    description: str = ""
    owner: str = ""
    version: str = "1.0.0"
    methods: List[str] = []


class ServiceResponse(BaseModel):
    id: int
    name: str
    description: str
    owner: str
    version: str
    methods: List[str]
    status: str
    created_at: datetime
    updated_at: datetime

    class Config:
        from_attributes = True


class ServiceInstanceCreate(BaseModel):
    service_name: str
    node_ip: str
    node_port: int
    weight: int = 1


class ServiceInstanceResponse(BaseModel):
    id: int
    service_name: str
    node_ip: str
    node_port: int
    weight: int
    status: str
    created_at: datetime

    class Config:
        from_attributes = True


# ── Config schemas ────────────────────────────────────────────

class ConfigSet(BaseModel):
    scope: str = Field(..., description="global / service / method")
    scope_key: str = Field(default="", description="service name or service/method")
    key: str
    value: str
    created_by: str = "admin"


class ConfigResponse(BaseModel):
    id: int
    scope: str
    scope_key: str
    key: str
    value: str
    version: int
    status: str
    created_by: str
    created_at: datetime

    class Config:
        from_attributes = True


class ConfigHistory(BaseModel):
    entries: List[ConfigResponse]


# ── Rate limit schemas ────────────────────────────────────────

class RateLimitRuleCreate(BaseModel):
    service_name: str = "*"
    method_name: str = "*"
    max_qps: int = 1000
    algorithm: str = "token_bucket"
    enabled: bool = True


class RateLimitRuleResponse(BaseModel):
    id: int
    service_name: str
    method_name: str
    max_qps: int
    algorithm: str
    enabled: bool
    created_at: datetime
    updated_at: datetime

    class Config:
        from_attributes = True


class RateLimitRuleUpdate(BaseModel):
    max_qps: Optional[int] = None
    algorithm: Optional[str] = None
    enabled: Optional[bool] = None


# ── Circuit breaker schemas ───────────────────────────────────

class CircuitBreakerRuleCreate(BaseModel):
    service_name: str = "*"
    method_name: str = "*"
    failure_threshold: int = 5
    timeout_sec: int = 30
    enabled: bool = True


class CircuitBreakerRuleResponse(BaseModel):
    id: int
    service_name: str
    method_name: str
    failure_threshold: int
    timeout_sec: int
    enabled: bool
    created_at: datetime
    updated_at: datetime

    class Config:
        from_attributes = True


class CircuitBreakerRuleUpdate(BaseModel):
    failure_threshold: Optional[int] = None
    timeout_sec: Optional[int] = None
    enabled: Optional[bool] = None


# ── Audit log schemas ─────────────────────────────────────────

class AuditLogResponse(BaseModel):
    id: int
    action: str
    resource_type: str
    resource_id: str
    detail: Dict[str, Any]
    operator: str
    created_at: datetime

    class Config:
        from_attributes = True


# ── Health/dash schemas ──────────────────────────────────────

class HealthResponse(BaseModel):
    status: str = "ok"
    version: str = "2.0.0"
    database: str = "connected"


class ClusterOverview(BaseModel):
    total_nodes: int
    online_nodes: int
    total_services: int
    total_instances: int
    active_rate_rules: int
    active_breaker_rules: int
