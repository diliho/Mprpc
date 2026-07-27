# Mprpc-EE Docker Compose Deployment

## Architecture

Uses `network_mode: host` for all containers to avoid DNS resolution issues with podman 3.4.4's CNI networking. All services communicate via `localhost`.

## Quick Start

```bash
cd deploy
podman-compose up -d
```

## Services

| Service | Port | Description |
|---------|------|-------------|
| MySQL | 3306 | Database |
| ZooKeeper | 2181 | Service Registry |
| Redis | 6379 | Cache & Rate Limiting |
| Prometheus | 9090 | Metrics Collection |
| Grafana | 3000 | Monitoring Dashboard (admin/admin) |
| AlertManager | 9093 | Alert Management |
| Control Plane | 8080 | API + Web Console |
| Provider-1 | 8001 (RPC), 9091 (metrics) | RPC Node 1 |
| Provider-2 | 8002 (RPC), 9092 (metrics) | RPC Node 2 |
| Provider-3 | 8003 (RPC), 9095 (metrics) | RPC Node 3 |

**Note**: Provider-3 uses metrics port 9095 because AlertManager binds both 9093 (web) and 9094 (cluster) on host networking.

## Access

- **Web Console**: http://localhost:8080/app/
- **API Docs**: http://localhost:8080/docs
- **Grafana**: http://localhost:3000 (admin/admin)
- **Prometheus**: http://localhost:9090/targets

## Register Providers with Control Plane

After starting, register providers with the control plane:

```bash
curl -X POST http://localhost:8080/api/v1/cluster/nodes \
  -H "Content-Type: application/json" \
  -d '{"ip": "127.0.0.1", "port": 8001}'

curl -X POST http://localhost:8080/api/v1/cluster/nodes \
  -H "Content-Type: application/json" \
  -d '{"ip": "127.0.0.1", "port": 8002}'

curl -X POST http://localhost:8080/api/v1/cluster/nodes \
  -H "Content-Type: application/json" \
  -d '{"ip": "127.0.0.1", "port": 8003}'
```

## Management

```bash
# Start all services
podman-compose up -d

# Stop all services
podman-compose down

# View logs
podman-compose logs -f

# Restart a specific service
podman-compose restart provider-1
```

## Known Issues

- **podman 3.4.4 CNI**: Bridge networking with DNS is broken (firewall plugin unsupported). All services use `host` networking.
- **ZooKeeper AdminServer disabled**: `ZOO_ADMINSERVER_ENABLED=false` to avoid port 8080 conflict with Control Plane.
- **ZooKeeper healthcheck**: Uses `srvr` command (not `ruok`) because only `srvr` is enabled in the 4-letter-word whitelist.
- **Provider metrics stub**: MetricsExporter returns 200 OK with empty body. Full Prometheus metric registration pending.
- **DB schema fix**: `nodes.ip` changed from UNIQUE to composite UNIQUE on `(ip, port)` to support multiple nodes on same IP.

## Troubleshooting

### Provider fails to start
- Check ZooKeeper is healthy: `podman-compose logs zookeeper`
- Check Redis is healthy: `podman-compose logs redis`

### Control Plane cannot connect to MySQL
- Check MySQL logs: `podman-compose logs mysql`
- Verify MySQL is healthy: `podman ps` (should show "healthy")

### Port conflicts
- Kill local services that use the same ports:
  ```bash
  # Check what's using a port
  sudo fuser <port>/tcp
  # Kill it
  sudo kill <pid>
  ```

### Metrics not showing in Prometheus
- Check provider metrics endpoint: `curl http://localhost:9091/metrics`
- Check Prometheus targets: http://localhost:9090/targets
