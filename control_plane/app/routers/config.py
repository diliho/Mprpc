"""Config center API - three-level config (global/service/method), version management."""

from typing import List, Optional
from fastapi import APIRouter, HTTPException, Query

from ..models.database import SessionLocal, ConfigVersion
from ..schemas.schemas import ConfigSet, ConfigResponse, ConfigHistory

router = APIRouter(prefix="/api/v1/config", tags=["config"])


def _log_audit(session, action, detail=None):
    from ..models.database import AuditLog
    session.add(AuditLog(action=action, resource_type="config", detail=detail or {}))


def _get_next_version(session, scope: str, scope_key: str, key: str) -> int:
    last = (
        session.query(ConfigVersion)
        .filter_by(scope=scope, scope_key=scope_key, key=key)
        .order_by(ConfigVersion.version.desc())
        .first()
    )
    return (last.version + 1) if last else 1


@router.post("", response_model=ConfigResponse, status_code=201)
def set_config(cfg: ConfigSet):
    """Set a configuration value (creates a new version)."""
    with SessionLocal() as session:
        ver = _get_next_version(session, cfg.scope, cfg.scope_key, cfg.key)
        db_cfg = ConfigVersion(
            scope=cfg.scope,
            scope_key=cfg.scope_key,
            key=cfg.key,
            value=cfg.value,
            version=ver,
            status="pending",
            created_by=cfg.created_by,
        )
        session.add(db_cfg)
        _log_audit(session, "config.set", {
            "scope": cfg.scope, "scope_key": cfg.scope_key,
            "key": cfg.key, "version": ver,
        })
        session.commit()
        session.refresh(db_cfg)
        return db_cfg


@router.get("", response_model=List[ConfigResponse])
def list_configs(
    scope: Optional[str] = Query(None),
    scope_key: Optional[str] = Query(None),
    key: Optional[str] = Query(None),
    latest_only: bool = Query(False),
):
    """List configurations, optionally filtered."""
    with SessionLocal() as session:
        q = session.query(ConfigVersion)
        if scope:
            q = q.filter(ConfigVersion.scope == scope)
        if scope_key:
            q = q.filter(ConfigVersion.scope_key == scope_key)
        if key:
            q = q.filter(ConfigVersion.key == key)

        if latest_only:
            from sqlalchemy import func
            subq = (
                session.query(
                    ConfigVersion.scope,
                    ConfigVersion.scope_key,
                    ConfigVersion.key,
                    func.max(ConfigVersion.version).label("max_ver"),
                )
                .group_by(ConfigVersion.scope, ConfigVersion.scope_key, ConfigVersion.key)
                .subquery()
            )
            q = q.join(
                subq,
                (ConfigVersion.scope == subq.c.scope)
                & (ConfigVersion.scope_key == subq.c.scope_key)
                & (ConfigVersion.key == subq.c.key)
                & (ConfigVersion.version == subq.c.max_ver),
            )

        return q.order_by(ConfigVersion.id.desc()).limit(200).all()


@router.get("/latest", response_model=ConfigResponse)
def get_latest_config(scope: str, scope_key: str, key: str):
    """Get the latest version of a specific config key."""
    with SessionLocal() as session:
        cfg = (
            session.query(ConfigVersion)
            .filter_by(scope=scope, scope_key=scope_key, key=key)
            .order_by(ConfigVersion.version.desc())
            .first()
        )
        if not cfg:
            raise HTTPException(404, "Config not found")
        return cfg


@router.get("/history", response_model=ConfigHistory)
def config_history(scope: str, scope_key: str, key: str):
    """Get version history for a config key."""
    with SessionLocal() as session:
        entries = (
            session.query(ConfigVersion)
            .filter_by(scope=scope, scope_key=scope_key, key=key)
            .order_by(ConfigVersion.version.desc())
            .limit(50)
            .all()
        )
        return ConfigHistory(entries=entries)


@router.post("/{config_id}/apply", response_model=ConfigResponse)
def apply_config(config_id: int):
    """Mark a config version as applied."""
    with SessionLocal() as session:
        cfg = session.query(ConfigVersion).get(config_id)
        if not cfg:
            raise HTTPException(404, "Config not found")
        cfg.status = "applied"
        _log_audit(session, "config.apply", {"config_id": config_id, "version": cfg.version})
        session.commit()
        session.refresh(cfg)
        return cfg


@router.post("/{config_id}/rollback", response_model=ConfigResponse)
def rollback_config(config_id: int):
    """Rollback a config version."""
    with SessionLocal() as session:
        cfg = session.query(ConfigVersion).get(config_id)
        if not cfg:
            raise HTTPException(404, "Config not found")
        cfg.status = "rolled_back"
        _log_audit(session, "config.rollback", {"config_id": config_id, "version": cfg.version})
        session.commit()
        session.refresh(cfg)
        return cfg
