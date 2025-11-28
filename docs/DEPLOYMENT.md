# Titan API Gateway - Deployment Guide

Comprehensive guide for deploying Titan in production environments.

## Table of Contents

1. [Overview](#overview)
2. [Architecture Scenarios](#architecture-scenarios)
3. [Build Options](#build-options)
4. [Deployment Methods](#deployment-methods)
5. [Kubernetes Deployment](#kubernetes-deployment)
6. [Docker Deployment](#docker-deployment)
7. [Bare Metal Deployment](#bare-metal-deployment)
8. [Configuration](#configuration)
9. [Security](#security)
10. [Performance Tuning](#performance-tuning)
11. [Monitoring](#monitoring)
12. [Troubleshooting](#troubleshooting)

## Overview

Titan supports multiple deployment methods:

- **Kubernetes**: Scalable, production-ready with Helm charts (HA deployment)
- **Docker**: Containerized deployment with minimal image size (single-host)
- **Bare Metal**: systemd service for maximum performance (single-host)

## Architecture Scenarios

### Scenario 1: Single Titan Instance

**Use Case**: Development, small deployments, bare metal (systemd), single Docker container

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    Titan    │
                    │             │
                    │  Built-in   │
                    │Load Balancer│
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│ │Backend 3│
         └─────────┘  └─────────┘ └─────────┘
```

**Features**:
- One Titan entrypoint
- Titan's built-in load balancing (round-robin, weighted, least connections, random)
- Connection pooling per upstream
- TLS termination at Titan
- Suitable for: systemd deployment, Docker single-host

### Scenario 2: Multiple Titan Instances (HA)

**Use Case**: Production, Kubernetes, high availability, horizontal scaling

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
              ┌────────────▼────────────┐
              │   External Load         │
              │   Balancer (K8s)        │
              └────────────┬────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │ Titan 1 │  │ Titan 2 │ │ Titan N │
         │         │  │         │ │         │
         │Built-in │  │Built-in │ │Built-in │
         │   LB    │  │   LB    │ │   LB    │
         └────┬────┘  └────┬────┘ └────┬────┘
              │            │            │
              └────────────┼────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│ │Backend 3│
         └─────────┘  └─────────┘ └─────────┘
```

**Features**:
- Two layers of load balancing:
  1. **External LB**: Distributes across Titan instances (for HA/redundancy)
  2. **Titan's built-in LB**: Each instance load balances to backends
- Auto-scaling with Kubernetes HPA
- Rolling updates without downtime
- Pod anti-affinity for node distribution
- Suitable for: Kubernetes deployment, production clusters

## Build Options

### Development Build

Fast compilation with debug symbols:

```bash
cmake --preset=dev
cmake --build --preset=dev

# Output: build/dev/src/titan (31MB with debug symbols)
```

### Release Build

Optimized with LTO:

```bash
cmake --preset=release
cmake --build --preset=release

# Output: build/release/src/titan (7.3MB stripped)
```

### Static Build

For containerless deployment:

```bash
cmake --preset=static
cmake --build --preset=static

# Output: build/static/src/titan (7.3MB, minimal dependencies)
```

### Verify Build

```bash
# Check dependencies
ldd build/static/src/titan

# Output should show minimal dependencies:
# linux-vdso.so.1
# libc++.so.1
# libc++abi.so.1
# libc.so.6
# libm.so.6

# Test binary
./build/static/src/titan --version
./build/static/src/titan --help
```

## Deployment Methods

### Quick Comparison

| Method | Architecture | Use Case | Complexity | Scalability | Performance |
|--------|-------------|----------|------------|-------------|-------------|
| Kubernetes | Scenario 2 | Production multi-cluster | High | Excellent | Good |
| Docker | Scenario 1 | Single-host/dev | Low | Good | Good |
| Bare Metal | Scenario 1 | Maximum performance | Medium | Manual | Excellent |

## Kubernetes Deployment

**Architecture**: Scenario 2 (Multiple Titan instances with external LB)

### Prerequisites

- Kubernetes 1.25+
- kubectl configured
- Helm 3.8+ (for Helm deployment)
- Docker image in registry

### Option 1: Helm Chart (Recommended)

#### Development

```bash
helm install titan-dev ./deploy/helm/titan \
  --namespace titan-dev \
  --create-namespace \
  --values ./deploy/helm/titan/values-dev.yaml
```

#### Staging

```bash
helm install titan-staging ./deploy/helm/titan \
  --namespace titan-staging \
  --create-namespace \
  --values ./deploy/helm/titan/values-staging.yaml
```

#### Production

```bash
# Review configuration
helm template titan-prod ./deploy/helm/titan \
  --values ./deploy/helm/titan/values-production.yaml

# Deploy
helm install titan-prod ./deploy/helm/titan \
  --namespace titan-prod \
  --create-namespace \
  --values ./deploy/helm/titan/values-production.yaml

# Verify
kubectl get all -n titan-prod
kubectl get pods -n titan-prod -w
```

### Option 2: Raw Manifests

```bash
# Deploy all resources
kubectl apply -k deploy/kubernetes/

# Verify
kubectl get all -n titan
kubectl logs -n titan -l app=titan -f
```

### Scaling

```bash
# Manual scaling
kubectl scale deployment/titan-gateway --replicas=10 -n titan-prod

# Check HPA
kubectl get hpa -n titan-prod

# Describe HPA
kubectl describe hpa titan-gateway -n titan-prod
```

### Rolling Updates

```bash
# Update image
helm upgrade titan-prod ./deploy/helm/titan \
  --set image.tag=v0.2.0 \
  --reuse-values

# Watch rollout
kubectl rollout status deployment/titan-gateway -n titan-prod

# Rollback if needed
helm rollback titan-prod
```

## Docker Deployment

**Architecture**: Scenario 1 (Single Titan instance)

### Build Image

```bash
# Build production image
docker build -f Dockerfile.production -t titan:latest .

# Tag for registry
docker tag titan:latest ghcr.io/titan-gateway/titan:v0.1.0

# Push
docker push ghcr.io/titan-gateway/titan:v0.1.0
```

### Run Container

```bash
# Create config directory
mkdir -p /etc/titan
cp config/benchmark.json /etc/titan/config.json

# Run container
docker run -d \
  --name titan \
  --restart unless-stopped \
  -p 8080:8080 \
  -p 8443:8443 \
  -v /etc/titan:/etc/titan:ro \
  --user 1000:1000 \
  --cap-add NET_BIND_SERVICE \
  --ulimit nofile=1048576:1048576 \
  titan:latest

# Check logs
docker logs -f titan

# Health check
curl http://localhost:8080/_health
```

### Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  titan:
    image: titan:latest
    container_name: titan
    restart: unless-stopped
    ports:
      - "8080:8080"
      - "8443:8443"
    volumes:
      - ./config/config.json:/etc/titan/config.json:ro
      - ./certs:/etc/titan/tls:ro
    user: "1000:1000"
    cap_add:
      - NET_BIND_SERVICE
    ulimits:
      nofile:
        soft: 1048576
        hard: 1048576
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/_health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 10s
```

Deploy:

```bash
docker-compose up -d
docker-compose logs -f
```

## Bare Metal Deployment

**Architecture**: Scenario 1 (Single Titan instance)

### Automated Installation

```bash
# Build static binary
cmake --preset=static
cmake --build --preset=static

# Install
sudo ./deploy/systemd/install.sh
```

### Manual Installation

See [deploy/systemd/README.md](../deploy/systemd/README.md) for detailed steps.

### Service Management

```bash
# Start
sudo systemctl start titan

# Status
sudo systemctl status titan

# Logs
sudo journalctl -u titan -f

# Restart
sudo systemctl restart titan

# Stop
sudo systemctl stop titan
```

## Configuration

### Configuration File

Create `/etc/titan/config.json`:

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "https_port": 8443,
    "workers": 4,
    "tls": {
      "enabled": true,
      "cert_path": "/etc/titan/tls/tls.crt",
      "key_path": "/etc/titan/tls/tls.key",
      "min_version": "1.3"
    }
  },
  "upstreams": [
    {
      "name": "backend",
      "load_balancing": "weighted_round_robin",
      "health_check": {
        "enabled": true,
        "interval_seconds": 30,
        "timeout_seconds": 5,
        "path": "/health"
      },
      "backends": [
        {
          "host": "backend1.example.com",
          "port": 8080,
          "weight": 3,
          "max_connections": 200
        },
        {
          "host": "backend2.example.com",
          "port": 8080,
          "weight": 1,
          "max_connections": 100
        }
      ]
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "method": "GET",
      "upstream": "backend"
    }
  ],
  "middleware": [
    {
      "type": "rate_limit",
      "config": {
        "requests_per_second": 1000,
        "burst": 2000
      }
    },
    {
      "type": "cors",
      "config": {
        "allowed_origins": ["https://example.com"],
        "allowed_methods": ["GET", "POST", "PUT", "DELETE"],
        "allowed_headers": ["Content-Type", "Authorization"]
      }
    }
  ]
}
```

### Load Balancing Strategies

Titan includes built-in load balancing algorithms (works in both scenarios):

#### Round Robin (default)

Distributes requests evenly across backends:

```json
{
  "upstreams": [
    {
      "load_balancing": "round_robin",
      "backends": [
        {"host": "backend1", "port": 8080},
        {"host": "backend2", "port": 8080}
      ]
    }
  ]
}
```

#### Least Connections

Routes to backend with fewest active connections:

```json
{
  "upstreams": [
    {
      "load_balancing": "least_connections"
    }
  ]
}
```

#### Weighted Round Robin

Distributes based on backend weights (higher weight = more traffic):

```json
{
  "upstreams": [
    {
      "load_balancing": "weighted_round_robin",
      "backends": [
        {"host": "backend1", "port": 8080, "weight": 3},
        {"host": "backend2", "port": 8080, "weight": 1}
      ]
    }
  ]
}
```

Backend1 receives 75% of traffic, Backend2 receives 25%.

#### Random

Randomly selects backend for each request:

```json
{
  "upstreams": [
    {
      "load_balancing": "random"
    }
  ]
}
```

### Environment Variables

- `TITAN_WORKERS`: Number of worker threads (default: CPU cores)
- `TITAN_LOG_LEVEL`: Log level (debug, info, warn, error)

### TLS Certificates

#### Self-Signed (Development)

```bash
openssl req -x509 -newkey rsa:4096 \
  -keyout /etc/titan/tls/tls.key \
  -out /etc/titan/tls/tls.crt \
  -days 365 -nodes \
  -subj "/CN=localhost"
```

#### Let's Encrypt (Production)

```bash
# Using certbot
certbot certonly --standalone -d api.example.com

# Copy certificates
cp /etc/letsencrypt/live/api.example.com/fullchain.pem /etc/titan/tls/tls.crt
cp /etc/letsencrypt/live/api.example.com/privkey.pem /etc/titan/tls/tls.key
```

## Security

### Required Capabilities

Titan requires `CAP_NET_BIND_SERVICE` to bind to privileged ports (80, 443).

#### Kubernetes

```yaml
securityContext:
  capabilities:
    add:
      - NET_BIND_SERVICE
```

#### Docker

```bash
docker run --cap-add NET_BIND_SERVICE titan:latest
```

#### Bare Metal

```bash
# Using systemd (automatic)
sudo systemctl start titan

# Or set capability manually
sudo setcap cap_net_bind_service=+ep /usr/local/bin/titan
```

### Security Hardening

#### Non-Root User

Always run as non-root:

```bash
# Create dedicated user
useradd --system --no-create-home --shell /bin/false titan

# Run as titan user
sudo -u titan /usr/local/bin/titan --config /etc/titan/config.json
```

#### Read-Only Filesystem

Kubernetes:

```yaml
securityContext:
  readOnlyRootFilesystem: true
```

#### Drop Unnecessary Capabilities

```yaml
securityContext:
  capabilities:
    drop:
      - ALL
    add:
      - NET_BIND_SERVICE
```

## Performance Tuning

### System Limits

#### File Descriptors

Add to `/etc/security/limits.conf`:

```
* soft nofile 1048576
* hard nofile 1048576
```

Or systemd service:

```ini
[Service]
LimitNOFILE=1048576
```

#### Kernel Parameters

Add to `/etc/sysctl.conf`:

```
# Connection tracking
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.core.netdev_max_backlog = 65535

# Port range
net.ipv4.ip_local_port_range = 1024 65535

# TCP tuning
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_keepalive_time = 300
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_slow_start_after_idle = 0

# File descriptors
fs.file-max = 2097152

# Memory
vm.swappiness = 10
```

Apply:

```bash
sudo sysctl -p
```

### Worker Configuration

Set workers to match CPU cores:

```json
{
  "server": {
    "workers": 4
  }
}
```

Or auto-detect:

```bash
export TITAN_WORKERS=$(nproc)
```

### Connection Pooling

Configure per-upstream to reuse backend connections:

```json
{
  "upstreams": [
    {
      "backends": [
        {
          "max_connections": 200
        }
      ]
    }
  ]
}
```

## Monitoring

### Health Checks

```bash
# Internal health endpoint
curl http://localhost:8080/_health

# Expected response:
# {"status":"healthy","version":"0.1.0"}
```

### Metrics

If Prometheus integration is enabled:

```bash
curl http://localhost:8080/_metrics
```

### Logs

#### Kubernetes

```bash
kubectl logs -n titan -l app=titan -f
```

#### Docker

```bash
docker logs -f titan
```

#### systemd

```bash
sudo journalctl -u titan -f
```

### Resource Monitoring

```bash
# Kubernetes
kubectl top pods -n titan

# Docker
docker stats titan

# Bare metal
systemctl status titan
```

## Troubleshooting

### Common Issues

#### Port Already in Use

```bash
# Find process using port
sudo lsof -i :8080

# Kill process
sudo kill -9 <PID>
```

#### Permission Denied

```bash
# Check file ownership
ls -la /etc/titan/

# Fix ownership
sudo chown -R titan:titan /etc/titan
```

#### Health Check Failing

```bash
# Test endpoint directly
curl -v http://localhost:8080/_health

# Check logs
kubectl logs -n titan <pod-name>
sudo journalctl -u titan -n 100
```

#### High Memory Usage

```bash
# Check memory usage
kubectl top pods -n titan

# Set memory limits (Kubernetes)
kubectl set resources deployment/titan \
  --limits=memory=1Gi \
  --requests=memory=512Mi
```

#### Backend Connection Issues

```bash
# Check upstream connectivity
curl -v http://backend1.example.com:8080/health

# Verify load balancing strategy
cat /etc/titan/config.json | jq '.upstreams[].load_balancing'
```

### Debug Mode

Enable debug logging:

```bash
# Environment variable
export TITAN_LOG_LEVEL=debug

# Or in config
{
  "server": {
    "log_level": "debug"
  }
}
```

## Production Checklist

- [ ] Static binary built and tested
- [ ] Configuration reviewed and validated
- [ ] Load balancing strategy configured
- [ ] TLS certificates installed
- [ ] Resource limits configured
- [ ] Health checks enabled
- [ ] Monitoring configured
- [ ] Logging configured
- [ ] Auto-scaling configured (K8s Scenario 2)
- [ ] Backup strategy in place
- [ ] Disaster recovery tested
- [ ] Load testing completed
- [ ] Security hardening applied
- [ ] Documentation updated

## References

- [Kubernetes Documentation](https://kubernetes.io/docs/)
- [Docker Documentation](https://docs.docker.com/)
- [systemd Documentation](https://systemd.io/)
- [Helm Documentation](https://helm.sh/docs/)

## Support

For issues and questions:
- GitHub Issues: https://github.com/titan-gateway/titan/issues
- Documentation: https://github.com/titan-gateway/titan/docs
