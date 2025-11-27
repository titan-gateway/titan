---
sidebar_position: 2
title: Kubernetes Deployment
---

# Kubernetes Deployment

Deploy Titan on Kubernetes for production-grade high availability, auto-scaling, and zero-downtime updates.

## Architecture

Kubernetes deployment uses **Scenario 2: Multiple Titan Instances** with external load balancing:

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
              ┌────────────▼────────────┐
              │   Kubernetes Service    │
              │   (LoadBalancer/Ingress)│
              └────────────┬────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │ Titan 1 │  │ Titan 2 │ │ Titan N │
         │  (Pod)  │  │  (Pod)  │ │  (Pod)  │
         └────┬────┘  └────┬────┘ └────┬────┘
              │            │            │
              └────────────┼────────────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│  │Backend 3│
         └─────────┘  └─────────┘  └─────────┘
```

**Features:**
- **High Availability**: Multiple pod replicas across nodes
- **Auto-scaling**: Horizontal Pod Autoscaler based on CPU/memory
- **Rolling updates**: Zero-downtime deployments
- **Service mesh ready**: Integrates with Istio, Linkerd

## Prerequisites

Before deploying Titan on Kubernetes, ensure you have:

- **Kubernetes cluster** 1.25+ (managed or self-hosted)
- **kubectl** configured and authenticated
- **Helm** 3.8+ (for Helm deployment)
- **Container registry** access (e.g., ghcr.io, Docker Hub)
- **Ingress controller** (optional, for external access)

## Deployment Options

### Option 1: Helm Chart (Recommended)

Helm provides a streamlined deployment experience with parameterized configurations.

#### Installation

```bash
# Add Helm repository
helm repo add titan https://jonathanberhe.github.io/titan/charts
helm repo update

# Install with default values
helm install titan titan/titan \
  --namespace titan \
  --create-namespace
```

#### Development Environment

For local development or testing:

```bash
helm install titan-dev titan/titan \
  --namespace titan-dev \
  --create-namespace \
  --set replicaCount=1 \
  --set resources.requests.cpu=100m \
  --set resources.requests.memory=128Mi \
  --set autoscaling.enabled=false
```

#### Staging Environment

For pre-production validation:

```bash
helm install titan-staging titan/titan \
  --namespace titan-staging \
  --create-namespace \
  --values - <<EOF
replicaCount: 3
resources:
  requests:
    cpu: 500m
    memory: 512Mi
  limits:
    cpu: 2000m
    memory: 2Gi
autoscaling:
  enabled: true
  minReplicas: 3
  maxReplicas: 10
  targetCPUUtilizationPercentage: 70
service:
  type: LoadBalancer
EOF
```

#### Production Environment

For production deployments with full HA:

```bash
# Review configuration first
helm template titan-prod titan/titan \
  --namespace titan-prod \
  --values values-production.yaml

# Deploy
helm install titan-prod titan/titan \
  --namespace titan-prod \
  --create-namespace \
  --values values-production.yaml

# Verify deployment
kubectl get all -n titan-prod
kubectl get pods -n titan-prod -w
```

**Sample `values-production.yaml`:**

```yaml
replicaCount: 5

image:
  repository: ghcr.io/jonathanberhe/titan
  tag: "0.1.0"
  pullPolicy: IfNotPresent

resources:
  requests:
    cpu: 1000m
    memory: 1Gi
  limits:
    cpu: 4000m
    memory: 4Gi

autoscaling:
  enabled: true
  minReplicas: 5
  maxReplicas: 20
  targetCPUUtilizationPercentage: 70
  targetMemoryUtilizationPercentage: 80

service:
  type: LoadBalancer
  port: 443
  annotations:
    service.beta.kubernetes.io/aws-load-balancer-type: "nlb"

ingress:
  enabled: true
  className: nginx
  hosts:
    - host: api.example.com
      paths:
        - path: /
          pathType: Prefix
  tls:
    - secretName: titan-tls
      hosts:
        - api.example.com

config:
  server:
    port: 8080
    workers: 4
    tls:
      enabled: false  # TLS terminated at ingress
  upstreams:
    - name: backend-api
      load_balancing: least_connections
      backends:
        - host: backend-service
          port: 8080

podSecurityContext:
  runAsNonRoot: true
  runAsUser: 1000
  fsGroup: 1000

securityContext:
  readOnlyRootFilesystem: true
  allowPrivilegeEscalation: false
  capabilities:
    drop:
      - ALL
    add:
      - NET_BIND_SERVICE

affinity:
  podAntiAffinity:
    preferredDuringSchedulingIgnoredDuringExecution:
      - weight: 100
        podAffinityTerm:
          labelSelector:
            matchExpressions:
              - key: app.kubernetes.io/name
                operator: In
                values:
                  - titan
          topologyKey: kubernetes.io/hostname
```

### Option 2: Raw Kubernetes Manifests

For full control over Kubernetes resources:

```bash
# Clone repository
git clone https://github.com/JonathanBerhe/titan.git
cd titan/deploy/kubernetes

# Deploy all resources
kubectl apply -k .

# Verify
kubectl get all -n titan
kubectl logs -n titan -l app=titan -f
```

## Scaling

### Manual Scaling

Scale the deployment to a specific number of replicas:

```bash
# Scale up
kubectl scale deployment/titan-gateway --replicas=10 -n titan-prod

# Verify
kubectl get pods -n titan-prod
```

### Horizontal Pod Autoscaler (HPA)

Enable automatic scaling based on metrics:

```bash
# Check HPA status
kubectl get hpa -n titan-prod

# Describe HPA details
kubectl describe hpa titan-gateway -n titan-prod

# Example HPA configuration (via Helm values)
# autoscaling:
#   enabled: true
#   minReplicas: 5
#   maxReplicas: 20
#   targetCPUUtilizationPercentage: 70
```

HPA automatically scales Titan pods based on CPU/memory utilization, ensuring optimal resource usage and request handling.

## Rolling Updates

Deploy new versions with zero downtime:

```bash
# Update to new version
helm upgrade titan-prod titan/titan \
  --set image.tag=v0.2.0 \
  --reuse-values

# Watch rollout progress
kubectl rollout status deployment/titan-gateway -n titan-prod

# Check rollout history
kubectl rollout history deployment/titan-gateway -n titan-prod

# Rollback to previous version if needed
helm rollback titan-prod

# Or rollback using kubectl
kubectl rollout undo deployment/titan-gateway -n titan-prod
```

**Rolling update strategy** (configured in Helm values):

```yaml
strategy:
  type: RollingUpdate
  rollingUpdate:
    maxSurge: 1
    maxUnavailable: 0
```

This ensures at least one pod is always available during updates.

## Security

### Required Capabilities

Titan needs `NET_BIND_SERVICE` to bind to privileged ports (80, 443):

```yaml
securityContext:
  capabilities:
    add:
      - NET_BIND_SERVICE
```

### Security Hardening

**Run as non-root user:**

```yaml
podSecurityContext:
  runAsNonRoot: true
  runAsUser: 1000
  fsGroup: 1000
```

**Read-only root filesystem:**

```yaml
securityContext:
  readOnlyRootFilesystem: true
  allowPrivilegeEscalation: false
```

**Drop unnecessary capabilities:**

```yaml
securityContext:
  capabilities:
    drop:
      - ALL
    add:
      - NET_BIND_SERVICE
```

### Network Policies

Restrict network access using NetworkPolicy:

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: titan-network-policy
spec:
  podSelector:
    matchLabels:
      app: titan
  policyTypes:
    - Ingress
    - Egress
  ingress:
    - from:
        - namespaceSelector:
            matchLabels:
              name: ingress-nginx
      ports:
        - protocol: TCP
          port: 8080
  egress:
    - to:
        - namespaceSelector:
            matchLabels:
              name: backend-services
      ports:
        - protocol: TCP
          port: 8080
```

## Monitoring

### Health Checks

Configure liveness and readiness probes:

```yaml
livenessProbe:
  httpGet:
    path: /health
    port: 8080
  initialDelaySeconds: 10
  periodSeconds: 10
  timeoutSeconds: 5
  failureThreshold: 3

readinessProbe:
  httpGet:
    path: /ready
    port: 8080
  initialDelaySeconds: 5
  periodSeconds: 5
  timeoutSeconds: 3
  failureThreshold: 2
```

### Metrics

**Prometheus integration:**

```yaml
# ServiceMonitor for Prometheus Operator
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: titan
spec:
  selector:
    matchLabels:
      app: titan
  endpoints:
    - port: metrics
      interval: 30s
```

**View metrics:**

```bash
# Port-forward to Prometheus
kubectl port-forward -n monitoring svc/prometheus 9090:9090

# Query Titan metrics
http://localhost:9090/graph?g0.expr=titan_requests_total
```

### Logs

**View logs:**

```bash
# All Titan pods
kubectl logs -n titan -l app=titan -f

# Specific pod
kubectl logs -n titan titan-gateway-abc123 -f

# Previous pod instance (for crashloops)
kubectl logs -n titan titan-gateway-abc123 -f --previous
```

**Centralized logging (Loki/Elasticsearch):**

Titan outputs structured JSON logs that integrate seamlessly with log aggregation systems.

## Troubleshooting

### Pod Not Starting

Check pod status and events:

```bash
kubectl describe pod -n titan titan-gateway-abc123
kubectl get events -n titan --sort-by='.lastTimestamp'
```

Common issues:
- **ImagePullBackOff**: Registry authentication failure
- **CrashLoopBackOff**: Configuration error, check logs
- **Pending**: Resource constraints, check node capacity

### High CPU/Memory Usage

Check resource usage:

```bash
kubectl top pods -n titan
kubectl top nodes
```

Adjust resource requests/limits in Helm values:

```yaml
resources:
  requests:
    cpu: 2000m
    memory: 2Gi
  limits:
    cpu: 4000m
    memory: 4Gi
```

### Backend Connection Issues

Verify service discovery:

```bash
# Check backend service DNS
kubectl run -it --rm debug --image=curlimages/curl --restart=Never -- sh
curl http://backend-service.default.svc.cluster.local:8080/health

# Check network policies
kubectl describe networkpolicy -n titan
```

## Production Checklist

- [ ] Helm chart deployed with production values
- [ ] At least 3 replicas for high availability
- [ ] HPA configured (min 3, max 20)
- [ ] Resource requests and limits set appropriately
- [ ] Pod anti-affinity configured to spread across nodes
- [ ] TLS certificates configured (ingress or service)
- [ ] Security context: non-root user, read-only filesystem
- [ ] Network policies restrict traffic
- [ ] Health checks (liveness/readiness) configured
- [ ] Prometheus ServiceMonitor deployed
- [ ] Centralized logging configured
- [ ] Resource quotas set at namespace level
- [ ] Pod disruption budget configured
- [ ] Backup strategy for configuration
- [ ] Disaster recovery plan tested

## Next Steps

- **[Docker Deployment](./docker)** - Alternative containerized deployment
- **[Bare Metal Deployment](./bare-metal)** - Deploy directly on Linux servers
- **[Configuration Reference](../configuration/overview)** - Detailed configuration options
- **[Architecture Overview](../architecture/overview)** - Understand Titan's design
