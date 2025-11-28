# Titan API Gateway - Helm Chart

This Helm chart deploys Titan API Gateway on Kubernetes with configurable environment-specific values.

## Prerequisites

- Kubernetes 1.25+
- Helm 3.8+
- (Optional) cert-manager for TLS certificate management

## Installation

### Quick Start

```bash
# Add repository (if published)
helm repo add titan https://charts.example.com/titan
helm repo update

# Install with default values
helm install titan titan/titan

# Or install from local chart
helm install titan ./deploy/helm/titan
```

### Environment-Specific Deployment

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
helm install titan-prod ./deploy/helm/titan \
  --namespace titan-prod \
  --create-namespace \
  --values ./deploy/helm/titan/values-production.yaml
```

## Configuration

### Common Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `replicaCount` | Number of replicas | `3` |
| `image.repository` | Image repository | `ghcr.io/JonathanBerhe/titan` |
| `image.tag` | Image tag | `latest` |
| `image.pullPolicy` | Image pull policy | `IfNotPresent` |
| `service.type` | Service type | `ClusterIP` |
| `service.port` | HTTP port | `80` |
| `service.httpsPort` | HTTPS port | `443` |
| `service.adminPort` | Admin port (metrics, health) | `9090` |
| `ingress.enabled` | Enable ingress | `false` |
| `autoscaling.enabled` | Enable HPA | `true` |
| `autoscaling.minReplicas` | Minimum replicas | `3` |
| `autoscaling.maxReplicas` | Maximum replicas | `20` |
| `resources.requests.cpu` | CPU request | `1000m` |
| `resources.requests.memory` | Memory request | `512Mi` |
| `resources.limits.cpu` | CPU limit | `2000m` |
| `resources.limits.memory` | Memory limit | `1Gi` |

### Titan Configuration

The `config` section in values.yaml maps directly to Titan's configuration:

```yaml
config:
  server:
    host: "0.0.0.0"
    port: 8080
    workers: 4

  upstreams:
    - name: "backend"
      load_balancing: "round_robin"
      backends:
        - host: "backend-service"
          port: 8080

  routes:
    - path: "/api/*"
      method: "GET"
      upstream: "backend"

  middleware:
    - type: "rate_limit"
      config:
        requests_per_second: 100
        burst: 200
```

### TLS Configuration

#### Manual TLS Secret

```bash
# Create TLS secret
kubectl create secret tls titan-tls \
  --cert=tls.crt \
  --key=tls.key \
  -n titan

# Enable in values.yaml
tls:
  enabled: true
  secretName: titan-tls
```

#### cert-manager Integration

```bash
# Install cert-manager
kubectl apply -f https://github.com/cert-manager/cert-manager/releases/download/v1.13.0/cert-manager.yaml

# Enable in values.yaml
tls:
  enabled: true
  certManager:
    enabled: true
    issuer: letsencrypt-prod

ingress:
  enabled: true
  annotations:
    cert-manager.io/cluster-issuer: "letsencrypt-prod"
```

## Customization

### Override Values

Create a custom values file:

```yaml
# custom-values.yaml
replicaCount: 10

resources:
  limits:
    cpu: 4000m
    memory: 2Gi

config:
  upstreams:
    - name: "my-backend"
      backends:
        - host: "my-service.default.svc.cluster.local"
          port: 8080
```

Deploy with custom values:

```bash
helm install titan ./deploy/helm/titan \
  --values custom-values.yaml
```

### Override Individual Values

```bash
helm install titan ./deploy/helm/titan \
  --set replicaCount=5 \
  --set image.tag=v0.1.0 \
  --set autoscaling.maxReplicas=30
```

## Upgrading

### Rolling Update

```bash
# Update image tag
helm upgrade titan ./deploy/helm/titan \
  --set image.tag=v0.2.0 \
  --reuse-values

# Update with new values file
helm upgrade titan ./deploy/helm/titan \
  --values ./deploy/helm/titan/values-production.yaml
```

### View Pending Changes

```bash
helm diff upgrade titan ./deploy/helm/titan \
  --values values-production.yaml
```

## Rollback

```bash
# List releases
helm history titan

# Rollback to previous version
helm rollback titan

# Rollback to specific revision
helm rollback titan 3
```

## Uninstallation

```bash
# Uninstall release
helm uninstall titan

# Delete namespace
kubectl delete namespace titan
```

## Testing

### Lint Chart

```bash
helm lint ./deploy/helm/titan
```

### Dry Run

```bash
helm install titan ./deploy/helm/titan \
  --dry-run \
  --debug
```

### Template Rendering

```bash
# Render templates
helm template titan ./deploy/helm/titan

# With specific values
helm template titan ./deploy/helm/titan \
  --values values-production.yaml

# Save to file
helm template titan ./deploy/helm/titan > rendered.yaml
```

### Test Installation

```bash
# Install in test namespace
helm install titan-test ./deploy/helm/titan \
  --namespace titan-test \
  --create-namespace

# Verify
kubectl get all -n titan-test

# Clean up
helm uninstall titan-test -n titan-test
kubectl delete namespace titan-test
```

## Monitoring

### View Resources

```bash
# Get all resources
kubectl get all -n titan

# Get pods
kubectl get pods -n titan -l app.kubernetes.io/name=titan

# Describe deployment
kubectl describe deployment titan -n titan
```

### Logs

```bash
# View logs
kubectl logs -n titan -l app.kubernetes.io/name=titan -f

# Logs from specific pod
kubectl logs -n titan titan-xxxx-yyyy -f
```

### Metrics

```bash
# HPA status
kubectl get hpa -n titan

# Resource usage
kubectl top pods -n titan
```

## Troubleshooting

### Pods Not Starting

```bash
# Check pod events
kubectl describe pod -n titan titan-xxxx-yyyy

# Check deployment events
kubectl describe deployment -n titan titan

# View previous logs (if crashed)
kubectl logs -n titan titan-xxxx-yyyy --previous
```

### Configuration Issues

```bash
# Validate rendered templates
helm template titan ./deploy/helm/titan --debug

# Check ConfigMap
kubectl get configmap -n titan titan-config -o yaml

# Validate JSON config
kubectl get configmap -n titan titan-config -o jsonpath='{.data.config\.json}' | jq .
```

### Health Check Failures

```bash
# Port-forward to pod (admin port)
kubectl port-forward -n titan titan-xxxx-yyyy 9090:9090

# Test health endpoint (now on admin port 9090)
curl http://localhost:9090/health

# Test metrics endpoint
curl http://localhost:9090/metrics

# Check probe configuration
kubectl get pod -n titan titan-xxxx-yyyy -o yaml | grep -A 10 livenessProbe
```

**Note:** Health and metrics endpoints have been moved to a separate admin server on port 9090 for security. This prevents exposing internal endpoints on the public-facing port 8080.

## Advanced Configuration

### Node Affinity

```yaml
nodeSelector:
  nodeType: compute

affinity:
  nodeAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
      nodeSelectorTerms:
      - matchExpressions:
        - key: nodeType
          operator: In
          values:
          - compute
```

### Tolerations

```yaml
tolerations:
  - key: "dedicated"
    operator: "Equal"
    value: "api-gateway"
    effect: "NoSchedule"
```

### Resource Quotas

```yaml
resources:
  limits:
    cpu: 4000m
    memory: 2Gi
    ephemeral-storage: 10Gi
  requests:
    cpu: 2000m
    memory: 1Gi
    ephemeral-storage: 5Gi
```

### Network Policy

```yaml
networkPolicy:
  enabled: true
  ingress:
    - from:
      - namespaceSelector:
          matchLabels:
            name: ingress-nginx
      - podSelector:
          matchLabels:
            app: monitoring
  egress:
    - to:
      - namespaceSelector:
          matchLabels:
            name: backend
      ports:
      - protocol: TCP
        port: 8080
```

## Environment Comparison

| Feature | Development | Staging | Production |
|---------|------------|---------|------------|
| Replicas | 1 | 2 | 5 |
| Auto-scaling | Disabled | 2-10 | 5-50 |
| TLS | Disabled | Enabled | Enabled |
| Ingress | Disabled | Enabled | Enabled |
| Resources | 500m/256Mi | 750m/384Mi | 1000m/512Mi |
| Rate Limit | 10 rps | 50 rps | 1000 rps |
| Log Level | debug | info | warn |

## Best Practices

1. **Use specific image tags** - Never use `latest` in production
2. **Enable resource limits** - Prevent resource exhaustion
3. **Enable PodDisruptionBudget** - Ensure availability during updates
4. **Use secrets** - Store sensitive data in Kubernetes secrets
5. **Enable monitoring** - Use ServiceMonitor for Prometheus
6. **Use network policies** - Restrict traffic between pods
7. **Test upgrades** - Always test in staging first
8. **Backup configuration** - Version control all values files
9. **Use cert-manager** - Automate TLS certificate management
10. **Monitor health checks** - Ensure probes are configured correctly

## References

- [Helm Documentation](https://helm.sh/docs/)
- [Kubernetes Best Practices](https://kubernetes.io/docs/concepts/configuration/overview/)
- [Titan Documentation](https://github.com/JonathanBerhe/titan)

## Support

For issues and questions:
- GitHub Issues: https://github.com/JonathanBerhe/titan/issues
- Documentation: https://github.com/JonathanBerhe/titan/docs
