#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEB_URL="http://localhost:8080/app"

cleanup() {
    echo ""
    echo "Shutting down..."
    kill $(ss -tlnp 2>/dev/null | grep ':8080' | grep -oP 'pid=\K[0-9]+') 2>/dev/null
    pkill -f "mprpc.*provider" 2>/dev/null || true
    wait 2>/dev/null
    exit 0
}
trap cleanup SIGINT SIGTERM

echo "=== Mprpc-EE Quick Start ==="

# ── Redis ──
if redis-cli ping 2>/dev/null | grep -q PONG; then
    echo "[OK] Redis already running"
else
    echo "Starting Redis..."
    redis-server --daemonize yes
    sleep 1
    echo "[OK] Redis started"
fi

# ── ZooKeeper ──
zk_alive() {
    echo srvr | nc -w 2 127.0.0.1 2181 2>/dev/null | grep -q "Mode:"
}

if zk_alive; then
    echo "[OK] ZooKeeper already running"
else
    echo "Starting ZooKeeper via Docker..."
    docker rm -f zookeeper 2>/dev/null || true
    docker run -d --name zookeeper -p 2181:2181 zookeeper:3.7 >/dev/null
    for i in $(seq 1 15); do
        if zk_alive; then
            break
        fi
        sleep 1
    done
    if zk_alive; then
        echo "[OK] ZooKeeper started"
    else
        echo "[WARN] ZooKeeper health check timed out, but might still be starting"
    fi
fi

# ── Control Plane ──
OLD_PID=$(ss -tlnp 2>/dev/null | grep ':8080' | grep -oP 'pid=\K[0-9]+' | head -1)
if [ -n "$OLD_PID" ]; then
    echo "Killing old control plane (pid=$OLD_PID)..."
    kill "$OLD_PID" 2>/dev/null && sleep 1
fi
echo "Starting Control Plane..."
cd "$PROJECT_DIR/control_plane"
python3 -m app.main &
CP_PID=$!
sleep 2

# ── RPC Provider ──
echo "Starting RPC Provider..."
cd "$PROJECT_DIR"
./bin/provider -i test.conf > /tmp/provider.log 2>&1 &
PROVIDER_PID=$!
sleep 1
echo "[OK] RPC Provider started (pid=$PROVIDER_PID)"

echo ""
echo "====================================="
echo "  Web Console:  $WEB_URL"
echo "  API Docs:     http://localhost:8080/docs"
echo "  Press Ctrl+C to stop all services"
echo "====================================="
echo ""

# Open browser if available
if command -v xdg-open &>/dev/null; then
    xdg-open "$WEB_URL" 2>/dev/null || true
elif command -v sensible-browser &>/dev/null; then
    sensible-browser "$WEB_URL" 2>/dev/null || true
fi

wait
