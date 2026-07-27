"""Mprpc-EE Control Plane - FastAPI entry point.

Usage:
    cd control_plane
    python -m app.main

    # Or with uvicorn:
    uvicorn app.main:app --reload --port 8080

    # With MySQL:
    export MPRPC_DB_URL="mysql+pymysql://user:pass@localhost/mprpc_ee"
"""

import os
from pathlib import Path
from fastapi import FastAPI, Response
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from prometheus_client import generate_latest, CONTENT_TYPE_LATEST

from .models.database import init_db
from .routers import cluster, config, governance, audit
from .routers import monitor, trace, alert
from .schemas.schemas import HealthResponse

WEB_DIST = Path(__file__).resolve().parent.parent.parent / "web" / "dist"

_db_backend = "sqlite"
if os.getenv("MPRPC_DB_URL", "").startswith("mysql"):
    _db_backend = "mysql"

app = FastAPI(
    title="Mprpc-EE Control Plane",
    description="RPC cluster management, config center, service governance, monitoring & tracing API",
    version="3.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Mount Vue3 frontend static files
if WEB_DIST.exists():
    app.mount("/app", StaticFiles(directory=str(WEB_DIST), html=True), name="frontend")

# ── Phase 2 Routers ───────────────────────────────────────────
app.include_router(cluster.router)
app.include_router(config.router)
app.include_router(governance.router)
app.include_router(audit.router)

# ── Phase 3 Routers ───────────────────────────────────────────
app.include_router(monitor.router)
app.include_router(trace.router)
app.include_router(alert.router)


@app.on_event("startup")
def startup():
    init_db()


@app.get("/health", response_model=HealthResponse, tags=["system"])
def health():
    return HealthResponse(status="ok", version="3.0.0", database=_db_backend)


@app.get("/", tags=["system"])
def root():
    return {
        "service": "Mprpc-EE Control Plane",
        "version": "3.0.0",
        "docs": "/docs",
        "phase": 3,
    }


@app.get("/metrics", tags=["system"])
def metrics():
    return Response(content=generate_latest(), media_type=CONTENT_TYPE_LATEST)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app.main:app", host="0.0.0.0", port=8080, reload=True)
