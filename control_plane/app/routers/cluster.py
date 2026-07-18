"""Cluster management API - node registration, health check, topology."""

from datetime import datetime
from typing import List
from fastapi import APIRouter, HTTPException

from ..models.database import SessionLocal, Node, Service, ServiceInstance
from ..schemas.schemas import (
    NodeCreate, NodeResponse, NodeStatusUpdate,
    ServiceCreate, ServiceResponse,
    ServiceInstanceCreate, ServiceInstanceResponse,
    ClusterOverview,
)

router = APIRouter(prefix="/api/v1/cluster", tags=["cluster"])


def _log_audit(session, action, resource_type, resource_id="", detail=None):
    from ..models.database import AuditLog
    log = AuditLog(
        action=action,
        resource_type=resource_type,
        resource_id=str(resource_id),
        detail=detail or {},
    )
    session.add(log)


# ── Nodes ─────────────────────────────────────────────────────

@router.get("/nodes", response_model=List[NodeResponse])
def list_nodes(status: str = None):
    with SessionLocal() as session:
        q = session.query(Node)
        if status:
            q = q.filter(Node.status == status)
        return q.order_by(Node.id.desc()).all()


@router.post("/nodes", response_model=NodeResponse, status_code=201)
def register_node(node: NodeCreate):
    with SessionLocal() as session:
        existing = session.query(Node).filter_by(ip=node.ip, port=node.port).first()
        if existing:
            raise HTTPException(409, f"Node {node.ip}:{node.port} already registered")
        db_node = Node(**node.model_dump(), status="online", last_heartbeat=datetime.utcnow())
        session.add(db_node)
        _log_audit(session, "node.register", "node", f"{node.ip}:{node.port}")
        session.commit()
        session.refresh(db_node)
        return db_node


@router.get("/nodes/{node_id}", response_model=NodeResponse)
def get_node(node_id: int):
    with SessionLocal() as session:
        node = session.query(Node).get(node_id)
        if not node:
            raise HTTPException(404, "Node not found")
        return node


@router.put("/nodes/{node_id}/status", response_model=NodeResponse)
def update_node_status(node_id: int, update: NodeStatusUpdate):
    with SessionLocal() as session:
        node = session.query(Node).get(node_id)
        if not node:
            raise HTTPException(404, "Node not found")
        old_status = node.status
        node.status = update.status
        node.last_heartbeat = datetime.utcnow()
        _log_audit(session, "node.status_update", "node", str(node_id),
                   {"old": old_status, "new": update.status})
        session.commit()
        session.refresh(node)
        return node


@router.delete("/nodes/{node_id}")
def delete_node(node_id: int):
    with SessionLocal() as session:
        node = session.query(Node).get(node_id)
        if not node:
            raise HTTPException(404, "Node not found")
        _log_audit(session, "node.delete", "node", str(node_id),
                   {"ip": node.ip, "port": node.port})
        session.delete(node)
        session.commit()
        return {"detail": "deleted"}


# ── Services ──────────────────────────────────────────────────

@router.get("/services", response_model=List[ServiceResponse])
def list_services():
    with SessionLocal() as session:
        return session.query(Service).order_by(Service.id.desc()).all()


@router.post("/services", response_model=ServiceResponse, status_code=201)
def register_service(svc: ServiceCreate):
    with SessionLocal() as session:
        existing = session.query(Service).filter_by(name=svc.name).first()
        if existing:
            raise HTTPException(409, f"Service '{svc.name}' already exists")
        db_svc = Service(**svc.model_dump())
        session.add(db_svc)
        _log_audit(session, "service.register", "service", svc.name)
        session.commit()
        session.refresh(db_svc)
        return db_svc


@router.get("/services/{service_name}", response_model=ServiceResponse)
def get_service(service_name: str):
    with SessionLocal() as session:
        svc = session.query(Service).filter_by(name=service_name).first()
        if not svc:
            raise HTTPException(404, f"Service '{service_name}' not found")
        return svc


@router.delete("/services/{service_name}")
def delete_service(service_name: str):
    with SessionLocal() as session:
        svc = session.query(Service).filter_by(name=service_name).first()
        if not svc:
            raise HTTPException(404, f"Service '{service_name}' not found")
        _log_audit(session, "service.delete", "service", service_name)
        session.delete(svc)
        session.commit()
        return {"detail": "deleted"}


# ── Service Instances ─────────────────────────────────────────

@router.get("/services/{service_name}/instances", response_model=List[ServiceInstanceResponse])
def list_instances(service_name: str):
    with SessionLocal() as session:
        return session.query(ServiceInstance).filter_by(service_name=service_name).all()


@router.post("/services/{service_name}/instances", response_model=ServiceInstanceResponse, status_code=201)
def add_instance(service_name: str, inst: ServiceInstanceCreate):
    inst.service_name = service_name
    with SessionLocal() as session:
        existing = session.query(ServiceInstance).filter_by(
            service_name=service_name, node_ip=inst.node_ip, node_port=inst.node_port
        ).first()
        if existing:
            raise HTTPException(409, "Instance already exists")
        db_inst = ServiceInstance(**inst.model_dump())
        session.add(db_inst)
        _log_audit(session, "instance.add", "instance", f"{service_name}@{inst.node_ip}:{inst.node_port}")
        session.commit()
        session.refresh(db_inst)
        return db_inst


@router.delete("/services/{service_name}/instances/{instance_id}")
def remove_instance(service_name: str, instance_id: int):
    with SessionLocal() as session:
        inst = session.query(ServiceInstance).get(instance_id)
        if not inst or inst.service_name != service_name:
            raise HTTPException(404, "Instance not found")
        _log_audit(session, "instance.remove", "instance", str(instance_id))
        session.delete(inst)
        session.commit()
        return {"detail": "deleted"}


# ── Overview ──────────────────────────────────────────────────

@router.get("/overview", response_model=ClusterOverview)
def cluster_overview():
    from ..models.database import RateLimitRule, CircuitBreakerRule
    with SessionLocal() as session:
        total_nodes = session.query(Node).count()
        online_nodes = session.query(Node).filter_by(status="online").count()
        total_services = session.query(Service).count()
        total_instances = session.query(ServiceInstance).count()
        active_rate_rules = session.query(RateLimitRule).filter_by(enabled=True).count()
        active_breaker_rules = session.query(CircuitBreakerRule).filter_by(enabled=True).count()
        return ClusterOverview(
            total_nodes=total_nodes,
            online_nodes=online_nodes,
            total_services=total_services,
            total_instances=total_instances,
            active_rate_rules=active_rate_rules,
            active_breaker_rules=active_breaker_rules,
        )
