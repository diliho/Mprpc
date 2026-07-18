"""Service governance API - rate limit rules, circuit breaker rules."""

from typing import List, Optional
from fastapi import APIRouter, HTTPException

from ..models.database import SessionLocal, RateLimitRule, CircuitBreakerRule
from ..schemas.schemas import (
    RateLimitRuleCreate, RateLimitRuleResponse, RateLimitRuleUpdate,
    CircuitBreakerRuleCreate, CircuitBreakerRuleResponse, CircuitBreakerRuleUpdate,
)

router = APIRouter(prefix="/api/v1/governance", tags=["governance"])


def _log_audit(session, action, detail=None):
    from ..models.database import AuditLog
    session.add(AuditLog(action=action, resource_type="governance", detail=detail or {}))


# ── Rate Limit Rules ─────────────────────────────────────────

@router.get("/ratelimit", response_model=List[RateLimitRuleResponse])
def list_ratelimit_rules():
    with SessionLocal() as session:
        return session.query(RateLimitRule).order_by(RateLimitRule.id.desc()).all()


@router.post("/ratelimit", response_model=RateLimitRuleResponse, status_code=201)
def create_ratelimit_rule(rule: RateLimitRuleCreate):
    with SessionLocal() as session:
        db_rule = RateLimitRule(**rule.model_dump())
        session.add(db_rule)
        _log_audit(session, "ratelimit.create", rule.model_dump())
        session.commit()
        session.refresh(db_rule)
        return db_rule


@router.put("/ratelimit/{rule_id}", response_model=RateLimitRuleResponse)
def update_ratelimit_rule(rule_id: int, update: RateLimitRuleUpdate):
    with SessionLocal() as session:
        rule = session.query(RateLimitRule).get(rule_id)
        if not rule:
            raise HTTPException(404, "Rule not found")
        for field, value in update.model_dump(exclude_unset=True).items():
            setattr(rule, field, value)
        _log_audit(session, "ratelimit.update", {"rule_id": rule_id, **update.model_dump(exclude_unset=True)})
        session.commit()
        session.refresh(rule)
        return rule


@router.delete("/ratelimit/{rule_id}")
def delete_ratelimit_rule(rule_id: int):
    with SessionLocal() as session:
        rule = session.query(RateLimitRule).get(rule_id)
        if not rule:
            raise HTTPException(404, "Rule not found")
        _log_audit(session, "ratelimit.delete", {"rule_id": rule_id})
        session.delete(rule)
        session.commit()
        return {"detail": "deleted"}


# ── Circuit Breaker Rules ─────────────────────────────────────

@router.get("/circuitbreaker", response_model=List[CircuitBreakerRuleResponse])
def list_breaker_rules():
    with SessionLocal() as session:
        return session.query(CircuitBreakerRule).order_by(CircuitBreakerRule.id.desc()).all()


@router.post("/circuitbreaker", response_model=CircuitBreakerRuleResponse, status_code=201)
def create_breaker_rule(rule: CircuitBreakerRuleCreate):
    with SessionLocal() as session:
        db_rule = CircuitBreakerRule(**rule.model_dump())
        session.add(db_rule)
        _log_audit(session, "circuitbreaker.create", rule.model_dump())
        session.commit()
        session.refresh(db_rule)
        return db_rule


@router.put("/circuitbreaker/{rule_id}", response_model=CircuitBreakerRuleResponse)
def update_breaker_rule(rule_id: int, update: CircuitBreakerRuleUpdate):
    with SessionLocal() as session:
        rule = session.query(CircuitBreakerRule).get(rule_id)
        if not rule:
            raise HTTPException(404, "Rule not found")
        for field, value in update.model_dump(exclude_unset=True).items():
            setattr(rule, field, value)
        _log_audit(session, "circuitbreaker.update", {"rule_id": rule_id})
        session.commit()
        session.refresh(rule)
        return rule


@router.delete("/circuitbreaker/{rule_id}")
def delete_breaker_rule(rule_id: int):
    with SessionLocal() as session:
        rule = session.query(CircuitBreakerRule).get(rule_id)
        if not rule:
            raise HTTPException(404, "Rule not found")
        _log_audit(session, "circuitbreaker.delete", {"rule_id": rule_id})
        session.delete(rule)
        session.commit()
        return {"detail": "deleted"}
