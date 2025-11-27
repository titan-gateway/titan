# Titan Gateway - Kubernetes Deployment

This directory contains Kubernetes manifests for deploying Titan API Gateway.

## Prerequisites

- Kubernetes cluster (1.25+)
- `kubectl` configured
- Docker image built and pushed to registry
- (Optional) Ingress controller (Nginx, AWS ALB, etc.)
- (Optional) cert-manager for TLS certificates

## Quick Start

### 1. Build and Push Docker Image

```bash
# Build production image
docker build -f Dockerfile.production -t titan-gateway:latest .

# Tag for your registry
docker tag titan-gateway:latest ghcr.io/JonathanBerhe/titan:latest

# Push to registry
docker push ghcr.io/JonathanBerhe/titan:latest
```

### 2. Update Image Reference

Edit `kustomization.yaml` to use your image:

```yaml
images:
  - name: titan-gateway
    newName: ghcr.io/JonathanBerhe/titan
    newTag: latest
```

### 3. Deploy with Kustomize

```bash
# Deploy all resources
kubectl apply -k .

# Verify deployment
kubectl get all -n titan

# Check pod status
kubectl get pods -n titan -w

# View logs
kubectl logs -n titan -l app=titan -f
```

### 4. Alternative: Deploy Individual Manifests

```bash
# Create namespace
kubectl apply -f namespace.yaml

# Deploy ConfigMap
kubectl apply -f configmap.yaml

# Deploy application
kubectl apply -f deployment.yaml
kubectl apply -f service.yaml

# (Optional) Deploy HPA
kubectl apply -f hpa.yaml

# (Optional) Deploy Ingress
kubectl apply -f ingress.yaml
```

## Configuration

### ConfigMap

The `configmap.yaml` contains the default configuration. Customize it for your environment:

```bash
# Edit ConfigMap
kubectl edit configmap titan-config -n titan

# Or update from file
kubectl create configmap titan-config --from-file=config.json -n titan --dry-run=client -o yaml | kubectl apply -f -

# Restart pods to pick up changes
kubectl rollout restart deployment/titan-gateway -n titan
```

### TLS Certificates

Create TLS secret for HTTPS:

```bash
# From files
kubectl create secret tls titan-tls \
  --cert=path/to/tls.crt \
  --key=path/to/tls.key \
  -n titan

# Or using cert-manager (recommended)
# See: https://cert-manager.io/docs/
```

### Backend Services

Update the `backends` array in ConfigMap to point to your actual backend services:

```json
{
  "upstreams": [
    {
      "name": "backend",
      "backends": [
        {
          "host": "backend-service.default.svc.cluster.local",
          "port": 8080
        }
      ]
    }
  ]
}
```

## Scaling

### Manual Scaling

```bash
# Scale to 5 replicas
kubectl scale deployment/titan-gateway --replicas=5 -n titan
```

### Auto-Scaling (HPA)

The `hpa.yaml` enables automatic scaling based on CPU/memory:

- **Min replicas:** 3
- **Max replicas:** 20
- **CPU target:** 70%
- **Memory target:** 80%

```bash
# Check HPA status
kubectl get hpa -n titan

# Describe HPA
kubectl describe hpa titan-gateway -n titan
```

## Monitoring

### Health Checks

Titan provides the `/_health` endpoint for Kubernetes probes:

```bash
# Port-forward and test
kubectl port-forward -n titan svc/titan-gateway 8080:80

# Test health endpoint
curl http://localhost:8080/_health
```

### Logs

```bash
# View logs from all pods
kubectl logs -n titan -l app=titan -f

# View logs from specific pod
kubectl logs -n titan titan-gateway-xxx-yyy -f

# Previous pod logs (after crash)
kubectl logs -n titan titan-gateway-xxx-yyy --previous
```

### Metrics

If using Prometheus:

```bash
# Port-forward to access metrics
kubectl port-forward -n titan svc/titan-gateway 8080:80

# Scrape metrics (if /_metrics endpoint is implemented)
curl http://localhost:8080/_metrics
```

## Troubleshooting

### Pods Not Starting

```bash
# Check pod status
kubectl describe pod -n titan titan-gateway-xxx-yyy

# Check events
kubectl get events -n titan --sort-by='.lastTimestamp'

# Check resource limits
kubectl top pods -n titan
```

### Connection Issues

```bash
# Test service connectivity
kubectl run -it --rm debug --image=curlimages/curl --restart=Never -- \
  curl http://titan-gateway.titan.svc.cluster.local/_health

# Check service endpoints
kubectl get endpoints -n titan titan-gateway

# Check network policies
kubectl get networkpolicies -n titan
```

### Config Issues

```bash
# View current config
kubectl get configmap titan-config -n titan -o yaml

# Validate JSON syntax
kubectl get configmap titan-config -n titan -o jsonpath='{.data.config\.json}' | jq .
```

## Ingress Setup

### Nginx Ingress

```bash
# Install Nginx Ingress Controller
kubectl apply -f https://raw.githubusercontent.com/kubernetes/ingress-nginx/controller-v1.8.1/deploy/static/provider/cloud/deploy.yaml

# Deploy Titan Ingress
kubectl apply -f ingress.yaml

# Get external IP
kubectl get ingress -n titan
```

### AWS ALB Ingress

```bash
# Install AWS Load Balancer Controller
# See: https://docs.aws.amazon.com/eks/latest/userguide/aws-load-balancer-controller.html

# Update ingress.yaml to use alb annotations
# Deploy
kubectl apply -f ingress.yaml
```

## Production Checklist

- [ ] **Image Security:** Use specific image tags (not `latest`) and scan for vulnerabilities
- [ ] **Resource Limits:** Set appropriate CPU/memory requests and limits based on benchmarks
- [ ] **TLS Certificates:** Use cert-manager or external certificate management
- [ ] **Secrets Management:** Use external secret managers (AWS Secrets Manager, HashiCorp Vault)
- [ ] **Network Policies:** Restrict traffic between pods
- [ ] **RBAC:** Create service account with minimal permissions
- [ ] **Pod Security:** Enable Pod Security Standards (restricted)
- [ ] **Monitoring:** Set up Prometheus, Grafana, and alerting
- [ ] **Logging:** Configure log aggregation (ELK, Loki, CloudWatch)
- [ ] **Backup:** Backup ConfigMaps and Secrets
- [ ] **Disaster Recovery:** Test failover and recovery procedures
- [ ] **Load Testing:** Benchmark under production-like load

## Cleanup

```bash
# Delete all resources
kubectl delete -k .

# Or delete namespace (removes everything)
kubectl delete namespace titan
```

## Service Types

The `service.yaml` includes three service types:

1. **ClusterIP** (`titan-gateway`): Internal access only
2. **LoadBalancer** (`titan-gateway-external`): External access via cloud LB
3. **Headless** (`titan-gateway-headless`): For StatefulSet or direct pod access

Choose the appropriate service based on your use case.

## References

- [Kubernetes Documentation](https://kubernetes.io/docs/)
- [Kustomize](https://kustomize.io/)
- [cert-manager](https://cert-manager.io/)
- [Prometheus Operator](https://prometheus-operator.dev/)
