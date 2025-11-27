# Titan API Gateway - Comprehensive Benchmark Suite

## Overview

This benchmark suite compares Titan against industry-leading reverse proxies and API gateways under extreme load conditions:

- **Nginx** - Industry standard reverse proxy
- **HAProxy** - High-performance TCP/HTTP load balancer
- **Envoy** - Cloud-native proxy (CNCF)
- **Kong** - API gateway built on OpenResty/Nginx
- **Titan** - Our C++23 API gateway

## Test Methodology

### Test Environment
- **Location:** Inside `titan-dev` Docker container (isolated, reproducible)
- **Backend:** Nginx backend server on port 3001 (single instance, consistent load)
- **Tools:** wrk (HTTP/1.1), h2load (HTTP/2)
- **Duration:** 30 seconds per test (with 10s warmup)
- **Network:** localhost (eliminates network variability)

### Test Scenarios

#### HTTP/1.1 Tests
1. **Small Response (1KB)** - Latency-focused
   - 100 connections, 4 threads
   - Target: >100k req/s
   - Metric: P99 latency

2. **Medium Response (10KB)** - Balanced
   - 100 connections, 4 threads
   - Target: >50k req/s
   - Metric: Throughput + P95 latency

3. **Large Response (100KB)** - Throughput-focused
   - 100 connections, 4 threads
   - Target: >10k req/s
   - Metric: Throughput + bandwidth

4. **High Concurrency (1KB)** - Scalability test
   - 2000 connections, 4 threads
   - Target: Handle without errors
   - Metric: Error rate + P99.9 latency

5. **Keep-Alive Stress (1KB)** - Connection reuse
   - 100 connections, 100k requests
   - Target: Minimal overhead
   - Metric: Req/s consistency

#### HTTP/2 Tests
1. **Small Response (1KB)** - Multiplexing efficiency
   - 100 streams, 10 clients
   - Target: >80k req/s
   - Metric: Stream throughput

2. **Medium Response (10KB)** - Header compression
   - 100 streams, 10 clients
   - Target: >40k req/s
   - Metric: CPU efficiency

3. **High Concurrency (1KB)** - Stream handling
   - 1000 streams, 50 clients
   - Target: Low latency at scale
   - Metric: P99 latency

### Metrics Collected

For each test, we collect:
- **Throughput:** Requests/sec, Transfer/sec
- **Latency:** Avg, P50, P90, P95, P99, P99.9, Max
- **Resources:** CPU%, Memory (RSS)
- **Errors:** Connection errors, timeout errors, total errors
- **Connections:** Success rate, reuse efficiency

## Quick Start

### 1. Setup (One-time)

**Option A: Pre-installed (Recommended)**

If using the latest Dockerfile, all tools are pre-installed:
```bash
# Enter container - benchmarks ready to use!
docker exec -it titan-dev bash
cd /workspace/benchmarks
```

**Option B: Manual Install**

If using an older container image:
```bash
# Enter container
docker exec -it titan-dev bash

# Install all proxies
cd /workspace/benchmarks
./scripts/setup.sh
```

**What's Included:**
- ✅ wrk (HTTP/1.1 benchmarking)
- ✅ h2load (HTTP/2 benchmarking)
- ✅ Nginx 1.24+
- ✅ HAProxy 2.8+
- ✅ Envoy 1.28.0
- ✅ Python comparison tools (tabulate, matplotlib, psutil)
- ⊘ Kong 3.4+ (optional, can be slow to install)

### 2. Run HTTP/1.1 Benchmarks

```bash
# Run all HTTP/1.1 tests (takes ~5 minutes)
./scripts/run-http1.sh

# Or run specific scenario
./scripts/run-http1.sh small    # 1KB responses
./scripts/run-http1.sh medium   # 10KB responses
./scripts/run-http1.sh large    # 100KB responses
```

### 3. Run HTTP/2 Benchmarks

```bash
# Run all HTTP/2 tests (takes ~3 minutes)
./scripts/run-http2.sh

# Or run specific scenario
./scripts/run-http2.sh small
./scripts/run-http2.sh medium
```

### 4. Compare Results

```bash
# Generate comparison report
./scripts/compare.py results/http1-*.json

# Or with visualization
./scripts/compare.py results/http1-*.json --chart
```

### 5. Run Complete Suite

```bash
# Run everything and compare
./scripts/run-all.sh
```

## Output Format

### Console Output (During Tests)
```
========================================
Testing: Nginx (HTTP/1.1 - Small Response)
========================================
Starting backend on port 3001...
Starting Nginx on port 8080...
Running benchmark (30s)...

Results:
  Requests/sec:   125,432.12
  Latency (avg):  0.79ms
  Latency (p99):  2.34ms
  Transfer/sec:   122.49 MB/s
  Errors:         0

CPU: 45%, Memory: 23MB
```

### JSON Output (For Comparison)
```json
{
  "proxy": "nginx",
  "protocol": "http1",
  "scenario": "small",
  "timestamp": "2025-01-23T12:00:00Z",
  "metrics": {
    "requests_per_sec": 125432.12,
    "transfer_per_sec_mb": 122.49,
    "latency_avg_ms": 0.79,
    "latency_p50_ms": 0.65,
    "latency_p99_ms": 2.34,
    "cpu_percent": 45.0,
    "memory_mb": 23
  }
}
```

### Comparison Output
```
HTTP/1.1 Benchmark Results - Small Response (1KB)
================================================================
Proxy      Req/s       Avg(ms)   P99(ms)   CPU%   Mem(MB)  Winner
----------------------------------------------------------------
Titan      142,341     0.68      1.89      42     18       🏆
Nginx      125,432     0.79      2.34      45     23
HAProxy    118,234     0.84      2.67      48     21
Envoy       89,123     1.12      4.23      67     45
Kong        72,456     1.38      5.67      78     128

Titan is 13.5% faster than Nginx (2nd place)
Titan has 19.3% lower P99 latency than Nginx
```

## Configuration Details

### Backend Server
- Nginx backend on port 3001
- Configurable response sizes
- No artificial delays (pure proxy performance)
- Endpoints:
  - `/` - JSON response from Nginx
  - `/api` - JSON response from Nginx
  - `/small` - Small JSON response
  - `/medium` - Medium JSON response
  - `/large` - Large JSON response

### Proxy Configurations
All proxies use equivalent configurations:
- Single upstream backend
- No caching, compression, or transformations
- Minimal logging (for performance)
- HTTP/1.1 keep-alive enabled
- HTTP/2 enabled (where applicable)

See `configs/` directory for specific configurations.

## Benchmark Tools

### wrk (HTTP/1.1)
- C-based, minimal overhead
- Lua scripting support
- Reports: throughput, latency distribution
- Installation: `apt-get install wrk`

### h2load (HTTP/2)
- From nghttp2 project
- Supports multiplexing, streams
- Reports: similar to wrk
- Installation: `apt-get install nghttp2-client`

## Interpreting Results

### Key Metrics

1. **Requests/sec** - Higher is better
   - Indicates raw throughput capacity
   - Compare relative performance (% difference)

2. **P99 Latency** - Lower is better
   - Shows tail latency (worst-case UX)
   - Critical for user-facing APIs

3. **CPU%** - Lower is better at same throughput
   - Indicates efficiency
   - Matters for cost optimization

4. **Memory** - Lower is better
   - Shows resource footprint
   - Important for container density

### What Matters Most?

- **API Gateway:** Latency (P99) + CPU efficiency
- **Static Content:** Throughput (req/s)
- **Microservices:** CPU + Memory footprint

### Expected Performance Tiers

Based on architecture:

**Tier 1 (Fastest):**
- Titan (C++23, zero-copy, thread-per-core)
- Nginx (C, mature optimizations)
- HAProxy (C, optimized for proxy)

**Tier 2 (Fast):**
- Envoy (C++, feature-rich)

**Tier 3 (Functional):**
- Kong (Lua on Nginx, overhead from features)

## Troubleshooting

### "Port already in use"
```bash
# Kill all proxy processes
pkill -9 nginx haproxy envoy titan
```

### "Too many open files"
```bash
# Increase file descriptor limit
ulimit -n 65536
```

### "Backend not responding"
```bash
# Check backend health
curl http://localhost:3001/

# Restart backend
pkill -9 nginx
nginx -c /workspace/config/nginx-backend.conf
```

### "Benchmark takes forever"
- Reduce duration in scripts (edit `DURATION=30` to `DURATION=10`)
- Skip Kong tests (slowest to start)

## Advanced Usage

### Custom Scenarios

Create custom wrk script in `scenarios/`:

```lua
-- scenarios/custom.lua
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"test": "data"}'

function response(status, headers, body)
  if status ~= 200 then
    print("Error: " .. status)
  end
end
```

Run with:
```bash
wrk -t4 -c100 -d30s -s scenarios/custom.lua http://localhost:8080/api
```

### Continuous Benchmarking

Add to CI/CD:
```bash
#!/bin/bash
# Run benchmarks and fail if Titan regresses
./scripts/run-http1.sh small
if ! ./scripts/compare.py --check-regression results/http1-small-*.json; then
  echo "Performance regression detected!"
  exit 1
fi
```

## Contributing

To add a new proxy:

1. Add config to `configs/<proxy>.conf`
2. Add install step to `scripts/setup.sh`
3. Add start/stop logic to `scripts/utils.sh`
4. Test with `./scripts/run-http1.sh small`

## References

- [wrk documentation](https://github.com/wg/wrk)
- [h2load documentation](https://nghttp2.org/documentation/h2load.1.html)
- [Nginx performance tuning](https://www.nginx.com/blog/tuning-nginx/)
- [HAProxy performance](https://www.haproxy.com/documentation/hapee/latest/performance/)
- [Envoy performance](https://www.envoyproxy.io/docs/envoy/latest/faq/performance/how_fast_is_envoy)
