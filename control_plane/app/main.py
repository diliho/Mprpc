"""Mprpc-EE Control Plane - FastAPI entry point.

Usage:
    cd control_plane
    python -m app.main

    # Or with uvicorn:
    uvicorn app.main:app --reload --port 8080
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .models.database import init_db
from .routers import cluster, config, governance, audit
from .schemas.schemas import HealthResponse

app = FastAPI(
    title="Mprpc-EE Control Plane",
    description="RPC cluster management, config center, and service governance API",
    version="2.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Routers ───────────────────────────────────────────────────
app.include_router(cluster.router)
app.include_router(config.router)
app.include_router(governance.router)
app.include_router(audit.router)


@app.on_event("startup")
def startup():
    init_db()


@app.get("/health", response_model=HealthResponse, tags=["system"])
def health():
    return HealthResponse(status="ok", version="2.0.0", database="sqlite")


@app.get("/", tags=["system"])
def root():
    return {
        "service": "Mprpc-EE Control Plane",
        "version": "2.0.0",
        "docs": "/docs",
    }


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app.main:app", host="0.0.0.0", port=8080, reload=True)
