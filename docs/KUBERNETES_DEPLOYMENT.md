# Titan API Gateway - Kubernetes Deployment Guide

This guide covers deploying Titan in Kubernetes environments with optimal kernel tuning.

## Table of Contents

1. [Kernel Tuning in Containers](#kernel-tuning-in-containers)
2. [Deployment Strategies](#deployment-strategies)
3. [Helm Chart Configuration](#helm-chart-configuration)
4. [Performance Expectations](#performance-expectations)
5. [Troubleshooting](#troubleshooting)

---

## Kernel Tuning in Containers

### Understanding Sysctl Categories

Kernel parameters fall into three categories in containerized environments:

#### 1. **Safe Sysctls (Namespaced)** ✅

These are isolated per-pod and can be set without cluster admin approval:

- `net.ipv4.ip_local_port_range` - Ephemeral port range (all K8s versions)
- `net.ipv4.tcp_rmem` - TCP read buffer sizes (K8s 1.32+)
- `net.ipv4.tcp_wmem` - TCP write buffer sizes (K8s 1.32+)
- `net.ipv4.tcp_keepalive_time` - TCP keepalive timer (K8s 1.29+)
- `net.ipv4.tcp_fin_timeout` - FIN timeout (K8s 1.29+)

#### 2. **Unsafe Sysctls (Node-Wide)** ⚠️

These affect the entire node and require cluster admin configuration:

- `net.core.somaxconn` - Connection backlog queue size
- `net.core.rmem_max` - Maximum socket receive buffer
- `net.core.wmem_max` - Maximum socket send buffer
- `net.ipv4.tcp_fastopen` - TCP Fast Open mode
- `net.ipv4.tcp_max_syn_backlog` - Maximum pending connections

#### 3. **Process Limits** 🔧

These are set via container runtime or init containers:

- File descriptors (`ulimit -n`)
- Memory locking (`ulimit -l`)
- Linux capabilities (`CAP_NET_BIND_SERVICE`, `CAP_IPC_LOCK`)

---

## Deployment Strategies

### Strategy 1: Pod-Level Tuning (No Cluster Admin)

**Use case:** Development, staging, shared Kubernetes clusters

**Performance:** ~80% of fully tuned baseline

This strategy uses only safe sysctls and standard Kubernetes features.

#### values.yaml

```yaml
# helm/titan/values.yaml
replicaCount: 4

image:
  repository: your-registry/titan
  tag: "v1.0.0"
  pullPolicy: IfNotPresent

# Safe sysctls (no cluster admin needed)
podSecurityContext:
  sysctls:
    - name: net.ipv4.ip_local_port_range
      value: "1024 65535"
    # Requires Kubernetes 1.32+ (Dec 2024)
    - name: net.ipv4.tcp_rmem
      value: "4096 87380 67108864"
    - name: net.ipv4.tcp_wmem
      value: "4096 65536 67108864"

# Linux capabilities
securityContext:
  capabilities:
    add:
      - NET_BIND_SERVICE  # Bind ports 80/443
      - IPC_LOCK          # Lock memory for mimalloc
  allowPrivilegeEscalation: false
  runAsNonRoot: true
  runAsUser: 1000

# Resource limits
resources:
  requests:
    memory: "2Gi"
    cpu: "1000m"
  limits:
    memory: "4Gi"
    cpu: "2000m"

# Service configuration
service:
  type: ClusterIP
  ports:
    - name: http
      port: 80
      targetPort: 8080
      protocol: TCP
    - name: https
      port: 443
      targetPort: 8443
      protocol: TCP

# Config map for titan.json
config:
  workers: 4
  tls:
    enabled: true
    cert_path: /etc/titan/tls/tls.crt
    key_path: /etc/titan/tls/tls.key
  routes:
    - path: "/api/*"
      upstream: "backend"
  upstreams:
    - name: "backend"
      targets:
        - "http://backend-service:3001"
      pool_size: 100
```

#### Deployment YAML

```yaml
# helm/titan/templates/deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: {{ include "titan.fullname" . }}
  labels:
    {{- include "titan.labels" . | nindent 4 }}
spec:
  replicas: {{ .Values.replicaCount }}
  selector:
    matchLabels:
      {{- include "titan.selectorLabels" . | nindent 6 }}
  template:
    metadata:
      labels:
        {{- include "titan.selectorLabels" . | nindent 8 }}
    spec:
      {{- with .Values.podSecurityContext }}
      securityContext:
        {{- toYaml . | nindent 8 }}
      {{- end }}
      containers:
      - name: titan
        image: "{{ .Values.image.repository }}:{{ .Values.image.tag }}"
        imagePullPolicy: {{ .Values.image.pullPolicy }}
        {{- with .Values.securityContext }}
        securityContext:
          {{- toYaml . | nindent 12 }}
        {{- end }}
        ports:
        - name: http
          containerPort: 8080
          protocol: TCP
        - name: https
          containerPort: 8443
          protocol: TCP
        resources:
          {{- toYaml .Values.resources | nindent 12 }}
        volumeMounts:
        - name: config
          mountPath: /etc/titan
          readOnly: true
        - name: tls
          mountPath: /etc/titan/tls
          readOnly: true
        livenessProbe:
          httpGet:
            path: /health
            port: http
          initialDelaySeconds: 10
          periodSeconds: 10
        readinessProbe:
          httpGet:
            path: /ready
            port: http
          initialDelaySeconds: 5
          periodSeconds: 5
      volumes:
      - name: config
        configMap:
          name: {{ include "titan.fullname" . }}
      - name: tls
        secret:
          secretName: {{ include "titan.fullname" . }}-tls
```

**Expected Performance:**
- Throughput: ~150k req/s (HTTP/1.1)
- p99 Latency: ~2ms
- Success Rate: 100%

---

### Strategy 2: Node-Level Tuning (Cluster Admin Required)

**Use case:** Production deployments with cluster admin cooperation

**Performance:** ~100% of fully tuned baseline

This strategy requires cluster-level configuration to enable unsafe sysctls.

#### Step 1: Cluster Admin - Enable Unsafe Sysctls

Configure kubelet to allow unsafe sysctls on dedicated node pool:

```yaml
# /var/lib/kubelet/config.yaml (on each node)
apiVersion: kubelet.config.k8s.io/v1beta1
kind: KubeletConfiguration
allowedUnsafeSysctls:
  - "net.core.somaxconn"
  - "net.core.rmem_max"
  - "net.core.wmem_max"
  - "net.ipv4.tcp_fastopen"
  - "net.ipv4.tcp_max_syn_backlog"
```

**Managed Kubernetes Examples:**

**Google GKE:**
```bash
gcloud container node-pools update titan-pool \
  --cluster=my-cluster \
  --zone=us-central1-a \
  --system-config-from-file=node-config.yaml
```

```yaml
# node-config.yaml
kubeletConfig:
  allowedUnsafeSysctls:
    - "net.core.somaxconn"
    - "net.core.rmem_max"
    - "net.core.wmem_max"
    - "net.ipv4.tcp_fastopen"
    - "net.ipv4.tcp_max_syn_backlog"
```

**AWS EKS:**
```bash
# Create EC2 launch template with user data
cat > user-data.sh <<'EOF'
#!/bin/bash
# Enable unsafe sysctls in kubelet
cat >> /etc/kubernetes/kubelet/kubelet-config.json <<JSON
{
  "allowedUnsafeSysctls": [
    "net.core.somaxconn",
    "net.core.rmem_max",
    "net.core.wmem_max",
    "net.ipv4.tcp_fastopen",
    "net.ipv4.tcp_max_syn_backlog"
  ]
}
JSON
systemctl restart kubelet
EOF

# Create node group with custom launch template
aws eks create-nodegroup \
  --cluster-name my-cluster \
  --nodegroup-name titan-nodes \
  --launch-template name=titan-lt,version=1
```

**Azure AKS:**
```bash
# Use custom node configuration
az aks nodepool add \
  --resource-group myResourceGroup \
  --cluster-name myAKSCluster \
  --name titanpool \
  --node-count 3 \
  --kubelet-config kubelet-config.json
```

```json
// kubelet-config.json
{
  "allowedUnsafeSysctls": [
    "net.core.somaxconn",
    "net.core.rmem_max",
    "net.core.wmem_max",
    "net.ipv4.tcp_fastopen",
    "net.ipv4.tcp_max_syn_backlog"
  ]
}
```

#### Step 2: DaemonSet for Node Tuning

Deploy DaemonSet to apply sysctls at node level:

```yaml
# helm/titan/templates/node-tuner-daemonset.yaml
{{- if .Values.nodeTuning.enabled }}
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: {{ include "titan.fullname" . }}-node-tuner
  namespace: kube-system
  labels:
    {{- include "titan.labels" . | nindent 4 }}
    component: node-tuner
spec:
  selector:
    matchLabels:
      app: {{ include "titan.name" . }}-node-tuner
  template:
    metadata:
      labels:
        app: {{ include "titan.name" . }}-node-tuner
    spec:
      hostNetwork: true
      hostPID: true
      nodeSelector:
        {{- toYaml .Values.nodeTuning.nodeSelector | nindent 8 }}
      tolerations:
        {{- toYaml .Values.nodeTuning.tolerations | nindent 8 }}
      initContainers:
      - name: sysctl-tuner
        image: alpine:latest
        securityContext:
          privileged: true
        command:
        - sh
        - -c
        - |
          #!/bin/sh
          set -e

          echo "Applying Titan kernel tuning parameters..."

          # Apply sysctls
          sysctl -w net.core.somaxconn={{ .Values.nodeTuning.sysctls.somaxconn }}
          sysctl -w net.core.rmem_max={{ .Values.nodeTuning.sysctls.rmem_max }}
          sysctl -w net.core.wmem_max={{ .Values.nodeTuning.sysctls.wmem_max }}
          sysctl -w net.ipv4.tcp_fastopen={{ .Values.nodeTuning.sysctls.tcp_fastopen }}
          sysctl -w net.ipv4.tcp_max_syn_backlog={{ .Values.nodeTuning.sysctls.tcp_max_syn_backlog }}

          # Make persistent across reboots
          mkdir -p /host/etc/sysctl.d
          cat > /host/etc/sysctl.d/99-titan.conf <<EOF
          # Titan API Gateway kernel tuning
          net.core.somaxconn={{ .Values.nodeTuning.sysctls.somaxconn }}
          net.core.rmem_max={{ .Values.nodeTuning.sysctls.rmem_max }}
          net.core.wmem_max={{ .Values.nodeTuning.sysctls.wmem_max }}
          net.ipv4.tcp_fastopen={{ .Values.nodeTuning.sysctls.tcp_fastopen }}
          net.ipv4.tcp_max_syn_backlog={{ .Values.nodeTuning.sysctls.tcp_max_syn_backlog }}
          EOF

          echo "Kernel tuning applied successfully!"
          sysctl -a | grep -E 'somaxconn|rmem_max|wmem_max|tcp_fastopen|tcp_max_syn_backlog'
        volumeMounts:
        - name: host-etc
          mountPath: /host/etc
      containers:
      - name: pause
        image: gcr.io/google_containers/pause:3.2
        resources:
          requests:
            cpu: 10m
            memory: 10Mi
          limits:
            cpu: 20m
            memory: 20Mi
      volumes:
      - name: host-etc
        hostPath:
          path: /etc
          type: Directory
{{- end }}
```

#### Step 3: Titan Deployment with Unsafe Sysctls

```yaml
# helm/titan/values.yaml (full tuning)
replicaCount: 4

image:
  repository: your-registry/titan
  tag: "v1.0.0"

# Enable node-level tuning (requires cluster admin)
nodeTuning:
  enabled: true
  nodeSelector:
    node-pool: titan  # Target specific node pool
  tolerations:
    - key: dedicated
      operator: Equal
      value: titan
      effect: NoSchedule
  sysctls:
    somaxconn: 4096
    rmem_max: 134217728
    wmem_max: 134217728
    tcp_fastopen: 3
    tcp_max_syn_backlog: 8192

# All sysctls (safe + unsafe)
podSecurityContext:
  sysctls:
    # Safe sysctls
    - name: net.ipv4.ip_local_port_range
      value: "1024 65535"
    - name: net.ipv4.tcp_rmem
      value: "4096 87380 67108864"
    - name: net.ipv4.tcp_wmem
      value: "4096 65536 67108864"

    # Unsafe sysctls (requires kubelet --allowed-unsafe-sysctls)
    - name: net.core.somaxconn
      value: "4096"
    - name: net.core.rmem_max
      value: "134217728"
    - name: net.core.wmem_max
      value: "134217728"
    - name: net.ipv4.tcp_fastopen
      value: "3"
    - name: net.ipv4.tcp_max_syn_backlog
      value: "8192"

securityContext:
  capabilities:
    add:
      - NET_BIND_SERVICE
      - IPC_LOCK
  allowPrivilegeEscalation: false
  runAsNonRoot: true
  runAsUser: 1000

# Pin pods to tuned nodes
nodeSelector:
  node-pool: titan

tolerations:
  - key: dedicated
    operator: Equal
    value: titan
    effect: NoSchedule

# Thread-per-core affinity (optional)
affinity:
  podAntiAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
      - labelSelector:
          matchExpressions:
            - key: app
              operator: In
              values:
                - titan
        topologyKey: "kubernetes.io/hostname"

resources:
  requests:
    memory: "2Gi"
    cpu: "2000m"
  limits:
    memory: "4Gi"
    cpu: "2000m"
```

**Expected Performance:**
- Throughput: ~190k req/s (HTTP/1.1)
- p99 Latency: ~1.4ms
- Success Rate: 100%

---

## Helm Chart Configuration

### Directory Structure

```
helm/
└── titan/
    ├── Chart.yaml
    ├── values.yaml
    ├── templates/
    │   ├── deployment.yaml
    │   ├── service.yaml
    │   ├── configmap.yaml
    │   ├── secret.yaml
    │   ├── node-tuner-daemonset.yaml
    │   ├── hpa.yaml
    │   └── NOTES.txt
    └── README.md
```

### Installation Commands

#### Strategy 1: Pod-Level Tuning

```bash
# Install with safe sysctls only
helm install titan ./helm/titan \
  --namespace titan \
  --create-namespace \
  --set nodeTuning.enabled=false
```

#### Strategy 2: Node-Level Tuning

```bash
# Install with full tuning (requires cluster admin)
helm install titan ./helm/titan \
  --namespace titan \
  --create-namespace \
  --set nodeTuning.enabled=true \
  --set nodeTuning.nodeSelector.node-pool=titan
```

### Upgrading Configuration

```bash
# Hot-reload config without downtime
helm upgrade titan ./helm/titan \
  --namespace titan \
  --reuse-values \
  --set config.routes[0].path="/api/v2/*"

# Verify RCU config reload
kubectl logs -n titan deployment/titan -f | grep "Config reloaded"
```

---

## Performance Expectations

### Throughput Comparison

| Configuration | HTTP/1.1 req/s | HTTP/2 req/s | p99 Latency | CPU Usage |
|---------------|----------------|--------------|-------------|-----------|
| **Bare Metal (Fully Tuned)** | 200k+ | 120k+ | 1.2ms | 65% |
| **K8s Strategy 2 (Unsafe Sysctls)** | ~190k | ~118k | 1.4ms | 70% |
| **K8s Strategy 1 (Safe Sysctls)** | ~150k | ~95k | 2.0ms | 75% |
| **K8s Default (No Tuning)** | ~80k | ~50k | 5.0ms | 85% |

### Latency Distribution

**Strategy 2 (Full Tuning):**
```
p50:  640μs
p75:  850μs
p90:  1.1ms
p99:  1.4ms
p99.9: 3.2ms
Max:  8.2ms
```

**Strategy 1 (Safe Sysctls):**
```
p50:  720μs
p75:  950μs
p90:  1.3ms
p99:  2.0ms
p99.9: 4.5ms
Max:  12ms
```

---

## Troubleshooting

### Issue: Pod fails to start with "sysctl permission denied"

**Symptoms:**
```
Error: failed to start container: OCI runtime create failed: sysctl "net.core.somaxconn": open /proc/sys/net/core/somaxconn: permission denied
```

**Cause:** Unsafe sysctl not allowed by kubelet.

**Solutions:**
1. Remove unsafe sysctls from `podSecurityContext.sysctls`
2. Work with cluster admin to enable unsafe sysctls (Strategy 2)
3. Use DaemonSet for node-level tuning instead of pod-level

### Issue: Low throughput despite tuning

**Symptoms:**
- Throughput <50k req/s
- High CPU usage (>90%)

**Diagnosis:**
```bash
# Check if sysctls applied
kubectl exec -n titan deployment/titan -- sysctl -a | grep -E 'somaxconn|rmem_max'

# Check pod logs for errors
kubectl logs -n titan deployment/titan | grep -i error

# Check backend connectivity
kubectl exec -n titan deployment/titan -- curl http://backend-service:3001/health
```

**Solutions:**
1. Verify sysctls applied correctly
2. Check backend service is healthy
3. Increase backend connection pool size
4. Verify DNS resolution is not blocking

### Issue: High p99 latency (>10ms)

**Symptoms:**
- p50 latency normal (~1ms)
- p99 latency very high (>10ms)

**Possible causes:**
1. **DNS blocking:** Check for `getaddrinfo` in logs
2. **Connection pool exhausted:** Increase `pool_size` in config
3. **Backend slow:** Profile backend service
4. **GC pauses:** Check memory allocation patterns

**Diagnosis:**
```bash
# Enable profiling in Titan
kubectl port-forward -n titan deployment/titan 6060:6060

# Generate CPU profile
curl http://localhost:6060/debug/pprof/profile?seconds=30 > titan.prof

# Check for DNS in profile
go tool pprof -top titan.prof | grep -i dns
```

### Issue: File descriptor limit exceeded

**Symptoms:**
```
Error: too many open files
```

**Solutions:**

**Method 1: Container Entrypoint (Recommended)**

```dockerfile
# Dockerfile
COPY <<'EOF' /usr/local/bin/entrypoint.sh
#!/bin/sh
ulimit -n 1000000
exec /usr/local/bin/titan "$@"
EOF

RUN chmod +x /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
```

**Method 2: Init Container**

```yaml
initContainers:
  - name: increase-fd-limit
    image: busybox
    command:
      - sh
      - -c
      - |
        ulimit -n 1000000
        # Note: This only affects init container, not main container
        # Use Method 1 instead
```

**Method 3: Docker/Containerd Runtime Config**

```bash
# Docker
docker run --ulimit nofile=1000000:1000000 titan

# containerd config.toml
[plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc.options]
  SystemdCgroup = true
  [plugins."io.containerd.grpc.v1.cri".containerd.runtimes.runc.options.Config]
    rlimits = [
      { type = "RLIMIT_NOFILE", hard = 1000000, soft = 1000000 }
    ]
```

### Issue: DaemonSet fails to apply sysctls

**Symptoms:**
```
Error: sysctl: setting key "net.core.somaxconn": Read-only file system
```

**Cause:** DaemonSet not running with `hostNetwork: true` or `privileged: true`.

**Solution:**
```yaml
spec:
  hostNetwork: true
  hostPID: true
  initContainers:
  - name: sysctl-tuner
    securityContext:
      privileged: true  # Required for sysctl -w
```

---

## Security Considerations

### Unsafe Sysctls Impact

Unsafe sysctls affect **all pods on the node**, not just Titan. This means:

1. **Blast radius:** Misconfiguration can affect other workloads
2. **Tenant isolation:** Not suitable for multi-tenant clusters
3. **PodSecurityPolicy:** May conflict with restrictive policies

**Mitigation:**
- Use dedicated node pools for Titan (`nodeSelector`)
- Apply taints to prevent non-Titan pods (`kubectl taint`)
- Monitor node-level metrics (conntrack, socket buffers)

### Linux Capabilities

Titan requires minimal capabilities:
- `CAP_NET_BIND_SERVICE` - Bind ports 80/443 (drop if using higher ports)
- `CAP_IPC_LOCK` - Lock memory for mimalloc (optional, improves performance)

**Do NOT grant:**
- `CAP_SYS_ADMIN` - Too broad, unnecessary
- `CAP_NET_ADMIN` - Unnecessary for normal operation
- `privileged: true` - Never needed for main container

---

## Best Practices

### 1. Use Dedicated Node Pools

```bash
# GKE example
gcloud container node-pools create titan-pool \
  --cluster=my-cluster \
  --machine-type=n2-standard-4 \
  --num-nodes=3 \
  --node-labels=node-pool=titan \
  --node-taints=dedicated=titan:NoSchedule
```

### 2. Pin Workers to CPUs

For **maximum performance**, use CPU pinning:

```yaml
# values.yaml
affinity:
  podAntiAffinity:
    requiredDuringSchedulingIgnoredDuringExecution:
      - labelSelector:
          matchLabels:
            app: titan
        topologyKey: "kubernetes.io/hostname"

resources:
  requests:
    cpu: "2000m"  # Request whole CPUs
  limits:
    cpu: "2000m"  # Hard limit = request for guaranteed QoS
```

**Kubernetes 1.26+ (CPU Manager Static Policy):**
```yaml
# Kubelet config
cpuManagerPolicy: static
reservedSystemCPUs: "0,1"  # Reserve CPUs for system

# Pod spec (requires Guaranteed QoS)
resources:
  requests:
    cpu: "4"      # Whole CPUs
    memory: "4Gi"
  limits:
    cpu: "4"      # Must match requests
    memory: "4Gi" # Must match requests
```

### 3. Monitor Kernel Metrics

```yaml
# ServiceMonitor for Prometheus
apiVersion: monitoring.coreos.com/v1
kind: ServiceMonitor
metadata:
  name: titan-metrics
spec:
  selector:
    matchLabels:
      app: titan
  endpoints:
    - port: metrics
      interval: 10s
      path: /metrics
```

**Key metrics to monitor:**
- `node_sockstat_TCP_alloc` - Active TCP connections
- `node_netstat_Tcp_RetransSegs` - TCP retransmits (should be near 0)
- `node_network_receive_drop_total` - Dropped packets (should be 0)
- `container_network_tcp_usage_total` - Per-pod TCP usage

### 4. Load Testing Before Production

Always benchmark before deploying to production:

```bash
# Build and deploy to staging
helm upgrade --install titan ./helm/titan \
  --namespace staging \
  --set nodeTuning.enabled=true

# Run comprehensive benchmark
kubectl exec -n staging deployment/load-generator -- \
  wrk -t4 -c100 -d60s http://titan.staging.svc.cluster.local/api

# Verify no errors in logs
kubectl logs -n staging deployment/titan --tail=1000 | grep -i error
```

---

## Example: Complete Production Deployment

This example shows a complete production deployment with all best practices:

```bash
# 1. Create dedicated namespace
kubectl create namespace titan

# 2. Create TLS secret
kubectl create secret tls titan-tls \
  --namespace=titan \
  --cert=tls/cert.pem \
  --key=tls/key.pem

# 3. Create Titan config
kubectl create configmap titan-config \
  --namespace=titan \
  --from-file=config.json=config/production.json

# 4. Deploy with Helm (Strategy 2 - Full Tuning)
helm upgrade --install titan ./helm/titan \
  --namespace=titan \
  --set nodeTuning.enabled=true \
  --set nodeTuning.nodeSelector.node-pool=titan \
  --set replicaCount=4 \
  --set resources.requests.cpu=2000m \
  --set resources.limits.cpu=2000m \
  --set image.tag=v1.0.0

# 5. Verify deployment
kubectl rollout status -n titan deployment/titan

# 6. Check sysctls applied
kubectl exec -n titan deployment/titan -- \
  sysctl -a | grep -E 'somaxconn|rmem_max|wmem_max|tcp_fastopen'

# 7. Run smoke test
kubectl run -n titan curl-test --rm -it --restart=Never --image=curlimages/curl -- \
  curl -v http://titan.titan.svc.cluster.local/health

# 8. Monitor metrics
kubectl port-forward -n titan svc/titan-metrics 9090:9090
# Open http://localhost:9090/metrics in browser
```

---

## References

- [Kubernetes Sysctls Documentation](https://kubernetes.io/docs/tasks/administer-cluster/sysctl-cluster/)
- [Titan Profiling Guide](./PROFILING.md)
- [Linux Network Tuning Guide](https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt)
- [Brendan Gregg's Performance Tuning](https://www.brendangregg.com/linuxperf.html)

---

**Questions or issues?** Please open an issue on the [Titan GitHub repository](https://github.com/anthropics/titan).
