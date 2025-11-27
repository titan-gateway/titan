---
sidebar_position: 1
title: Deployment Overview
---

# Deployment Overview

Titan supports multiple deployment strategies, from simple Docker containers to production Kubernetes clusters. Choose the method that best fits your infrastructure and scale requirements.

## Deployment Methods

| Method | Best For | Complexity | HA Support |
|--------|----------|------------|------------|
| [Docker](./docker) | Development, single-host production | Low | No |
| [Kubernetes](./kubernetes) | Cloud-native, high availability | Medium | Yes |
| [Bare Metal](./bare-metal) | Maximum performance, dedicated servers | Low | No |

### Docker

**Quick start with containers:**

```bash
docker pull ghcr.io/jonathanberhe/titan:latest
docker run -d -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json \
  ghcr.io/jonathanberhe/titan:latest
```

**Use cases:**
- Local development and testing
- Single-host production deployments
- Quick prototyping and demos

[Read the Docker deployment guide →](./docker)

### Kubernetes

**Production-grade high availability:**

```bash
helm install titan titan/titan \
  --namespace titan \
  --create-namespace \
  --set replicaCount=5 \
  --set autoscaling.enabled=true
```

**Use cases:**
- Cloud-native applications
- Auto-scaling workloads
- Multi-region deployments
- High availability requirements

[Read the Kubernetes deployment guide →](./kubernetes)

### Bare Metal

**Maximum performance on dedicated hardware:**

```bash
# Install via systemd
sudo ./deploy/systemd/install.sh

# Manage service
sudo systemctl start titan
sudo systemctl status titan
```

**Use cases:**
- Dedicated servers
- On-premise infrastructure
- Maximum performance requirements
- Minimal overhead needs

[Read the bare metal deployment guide →](./bare-metal)

## Architecture Scenarios

Titan's deployment architecture adapts to your scale requirements:

### Scenario 1: Single Instance

**Best for**: Development, small deployments, bare metal, single Docker container

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    Titan    │
                    │  Built-in   │
                    │Load Balancer│
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│ │Backend 3│
         └─────────┘  └─────────┘  └─────────┘
```

**Features:**
- Single Titan instance as entry point
- Built-in load balancing (round-robin, weighted, least connections)
- Connection pooling per upstream
- TLS termination at Titan

**When to use:**
- Development environments
- Small-scale production (less than 10k req/s)
- Dedicated bare metal servers
- Docker single-host deployments

### Scenario 2: High Availability (HA)

**Best for**: Production, Kubernetes, cloud environments, horizontal scaling

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
              ┌────────────▼────────────┐
              │   External Load         │
              │   Balancer (K8s/Cloud)  │
              └────────────┬────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │ Titan 1 │  │ Titan 2 │ │ Titan N │
         │ Built-in│  │ Built-in│ │ Built-in│
         │   LB    │  │   LB    │  │   LB    │
         └────┬────┘  └────┬────┘ └────┬────┘
              │            │            │
              └────────────┼────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│ │Backend 3│
         └─────────┘  └─────────┘  └─────────┘
```

**Features:**
- Two-layer load balancing for redundancy
- Horizontal auto-scaling based on load
- Zero-downtime rolling updates
- Geographic distribution support

**When to use:**
- Production workloads requiring 99.9%+ uptime
- Variable load requiring auto-scaling
- Multi-region deployments
- Cloud-native applications (AWS, GCP, Azure)

## Build Options

Before deploying, build Titan with appropriate optimization flags:

### Development Build

```bash
cmake --preset=dev
cmake --build --preset=dev
```

Includes debug symbols, assertions, and verbose logging. Best for local development and testing.

### Release Build

```bash
cmake --preset=release
cmake --build --preset=release
```

Optimized with `-O3`, LTO (Link-Time Optimization), and CPU-specific optimizations. 30-40% faster than debug builds.

### Static Build (Bare Metal)

```bash
cmake --preset=static
cmake --build --preset=static
```

Statically linked binary with no runtime dependencies. Ideal for bare metal deployments on varied Linux distributions.

## Configuration Basics

All deployment methods use the same JSON configuration format:

```json
{
  "server": {
    "port": 8080,
    "workers": 4,
    "tls": {
      "enabled": true,
      "cert_file": "/etc/titan/certs/server.crt",
      "key_file": "/etc/titan/certs/server.key"
    }
  },
  "upstreams": [
    {
      "name": "api",
      "load_balancing": "round_robin",
      "backends": [
        { "host": "backend1", "port": 8080 },
        { "host": "backend2", "port": 8080 }
      ]
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "upstream": "api"
    }
  ]
}
```

**Hot reload** configuration without downtime:

```bash
# Docker
docker exec titan-container kill -HUP 1

# Kubernetes
kubectl exec -it titan-pod -- kill -HUP 1

# Bare Metal
sudo systemctl reload titan
```

[See full configuration reference →](../configuration/overview)

## Security Best Practices

Regardless of deployment method, follow these security guidelines:

### Run as Non-Root User

```yaml
# Kubernetes
podSecurityContext:
  runAsNonRoot: true
  runAsUser: 1000

# Docker
docker run --user 1000:1000 titan:latest

# Bare Metal (systemd)
User=titan
Group=titan
```

### Required Capabilities

Titan needs `CAP_NET_BIND_SERVICE` to bind to privileged ports (80, 443):

```bash
# Kubernetes - in pod security context
capabilities:
  add:
    - NET_BIND_SERVICE

# Docker
docker run --cap-add NET_BIND_SERVICE titan:latest

# Bare Metal
sudo setcap cap_net_bind_service=+ep /usr/local/bin/titan
```

### TLS Configuration

Always use TLS in production:

```json
{
  "server": {
    "tls": {
      "enabled": true,
      "cert_file": "/path/to/cert.pem",
      "key_file": "/path/to/key.pem",
      "protocols": ["TLSv1.2", "TLSv1.3"]
    }
  }
}
```

## Performance Tuning

### Worker Threads

Set workers to match CPU core count:

```json
{
  "server": {
    "workers": 8  // One worker per CPU core
  }
}
```

**Find core count:**

```bash
# Linux
nproc

# macOS
sysctl -n hw.ncpu
```

### System Limits

Increase file descriptor limits for high-concurrency:

**Kubernetes:**

```yaml
resources:
  limits:
    cpu: "4"
    memory: 4Gi
```

**Docker:**

```bash
docker run --ulimit nofile=65536:65536 titan:latest
```

**Bare Metal:**

```bash
# In /etc/systemd/system/titan.service
[Service]
LimitNOFILE=65536
```

## Monitoring

### Health Checks

All deployment methods expose health endpoints:

```bash
# Check health
curl http://localhost:8080/health

# Expected response
{"status":"ok"}
```

**Kubernetes:**

```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 8080
readinessProbe:
  httpGet:
    path: /ready
    port: 8080
```

### Metrics

Titan exposes Prometheus-compatible metrics:

```bash
# Scrape metrics
curl http://localhost:8080/metrics
```

Common metrics:
- `titan_requests_total` - Total requests processed
- `titan_request_duration_seconds` - Request latency histogram
- `titan_upstream_connections` - Backend connection pool size
- `titan_upstream_failures_total` - Backend failure count

### Logs

**Docker:**

```bash
docker logs -f titan-container
```

**Kubernetes:**

```bash
kubectl logs -f -l app=titan -n titan
```

**Bare Metal:**

```bash
sudo journalctl -u titan -f
```

## Troubleshooting

### Common Issues

**Port already in use:**

```bash
# Find process using port
sudo lsof -i :8080
sudo netstat -tulpn | grep :8080

# Change port in config or kill conflicting process
```

**Permission denied:**

```bash
# Ensure capability is set (bare metal)
getcap /usr/local/bin/titan

# Or run on non-privileged port (>1024)
```

**Backend connection failures:**

```bash
# Test backend connectivity
curl http://backend-host:backend-port/

# Check DNS resolution
nslookup backend-host

# Check firewall rules
```

## Production Checklist

Before deploying to production, verify:

- [ ] TLS certificates installed and auto-renewal configured
- [ ] Worker count matches CPU core count
- [ ] Resource limits configured appropriately
- [ ] Health check endpoints accessible
- [ ] Monitoring and alerting configured
- [ ] Log aggregation setup (Loki, Elasticsearch, CloudWatch)
- [ ] Backup strategy for configuration
- [ ] Disaster recovery plan tested
- [ ] Load testing completed
- [ ] Security hardening applied

## Next Steps

Choose your deployment method and follow the detailed guide:

- **[Docker Deployment](./docker)** - Containerized deployment for development and simple production
- **[Kubernetes Deployment](./kubernetes)** - Production-grade HA deployment with auto-scaling
- **[Bare Metal Deployment](./bare-metal)** - Maximum performance on dedicated Linux servers
- **[Configuration Reference](../configuration/overview)** - Complete configuration documentation
- **[Architecture Overview](../architecture/overview)** - Understand Titan's design principles
