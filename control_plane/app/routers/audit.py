"""Audit log API - query control plane operation history."""

from typing import List, Optional
from fastapi import APIRouter, Query

from ..models.database import SessionLocal, AuditLog
from ..schemas.schemas import AuditLogResponse

router = APIRouter(prefix="/api/v1/audit", tags=["audit"])


@router.get("/logs", response_model=List[AuditLogResponse])
def list_audit_logs(
    action: Optional[str] = Query(None),
    resource_type: Optional[str] = Query(None),
    limit: int = Query(100, le=500),
):
    with SessionLocal() as session:
        q = session.query(AuditLog)
        if action:
            q = q.filter(AuditLog.action == action)
        if resource_type:
            q = q.filter(AuditLog.resource_type == resource_type)
        return q.order_by(AuditLog.id.desc()).limit(limit).all()
