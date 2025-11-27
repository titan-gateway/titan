# Dockerfile Updates for Benchmark Suite

## What Was Added

The Dockerfile has been updated to pre-install all benchmark tools, making the container ready for immediate benchmarking without manual setup.

## Changes Made

### 1. Proxy Servers (lines 48-50)
```dockerfile
# Proxy servers for benchmarking
nginx \
haproxy \
```
- **Nginx 1.24+** - Industry-standard reverse proxy
- **HAProxy 2.8+** - High-performance load balancer

### 2. Envoy Installation (lines 83-93)
```dockerfile
# Install Envoy proxy (download binary from GitHub)
ENV ENVOY_VERSION=1.28.0
RUN ARCH="$(uname -m)" \
    && if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then \
        ENVOY_ARCH="aarch_64"; \
    else \
        ENVOY_ARCH="x86_64"; \
    fi \
    && wget -q "https://github.com/envoyproxy/envoy/releases/download/v${ENVOY_VERSION}/envoy-${ENVOY_VERSION}-linux-${ENVOY_ARCH}" \
        -O /usr/local/bin/envoy \
    && chmod +x /usr/local/bin/envoy
```
- **Envoy 1.28.0** - Cloud-native proxy (CNCF)
- Supports both ARM64 and x86_64 architectures
- Downloaded directly from GitHub releases

### 3. Python Comparison Tools (lines 66-70)
```dockerfile
# Install Python packages for benchmark comparison
RUN pip3 install --no-cache-dir --break-system-packages \
    tabulate \
    matplotlib \
    psutil
```
- **tabulate** - Pretty-print comparison tables
- **matplotlib** - Generate charts (optional)
- **psutil** - Monitor CPU/memory usage

## Already Present (No Changes Needed)

These tools were already in the Dockerfile:
- ✅ **wrk** (line 45) - HTTP/1.1 benchmarking
- ✅ **nghttp2-client** (line 47) - Includes h2load for HTTP/2
- ✅ **wget, ca-certificates** (line 52-53) - For Envoy download
- ✅ **Python3, pip** (lines 41-42) - For comparison scripts

## Image Size Impact

- **Previous size:** ~2.5 GB
- **New size:** ~2.8 GB (+300 MB)
- **Why acceptable:** Nginx, HAProxy, Envoy are essential for benchmarking

## Rebuild Instructions

```bash
# Rebuild the container with updated Dockerfile
docker compose build titan-dev

# Verify installations
docker compose run titan-dev bash -c "
  nginx -v && \
  haproxy -v && \
  envoy --version && \
  wrk --version && \
  h2load --version && \
  python3 -c 'import tabulate, matplotlib, psutil; print(\"Python packages OK\")'
"
```

## Benefits

1. **Zero Manual Setup** - All tools pre-installed
2. **Consistent Environment** - Everyone uses same versions
3. **Fast CI/CD** - No install step in benchmark scripts
4. **Cross-Platform** - Works on ARM64 and x86_64

## Alternative: Manual Setup

If you don't want to rebuild the image, you can still use:
```bash
cd /workspace/benchmarks
./scripts/setup.sh
```

This script will install the same tools at runtime (slower, but works with any image).

## Kong Note

Kong is **not** included in the Dockerfile because:
- Complex installation (requires database setup)
- Large size impact (~500 MB)
- Slower startup time
- Optional for benchmarks

To include Kong, run `./scripts/setup.sh` and select "yes" when prompted.
