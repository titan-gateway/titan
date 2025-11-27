# Titan Benchmarking - Quick Start

## TL;DR

```bash
# 0. Rebuild container (if using updated Dockerfile)
docker compose build titan-dev

# 1. Enter container
docker compose exec titan-dev bash

# 2. Build Titan release
cd /workspace
cmake --preset=release && cmake --build --preset=release

# 3. Run HTTP/1.1 benchmarks (~5 minutes)
cd benchmarks
./scripts/run-http1.sh small

# 4. Compare results
./scripts/compare.py results/*.json
```

**Note:** If you have the latest Dockerfile, all benchmark tools (nginx, haproxy, envoy, wrk, h2load) are pre-installed. Otherwise, run `./scripts/setup.sh` first.

## What You Get

**Console Output:**
```
========================================================================
Scenario: http1-small
========================================================================
Proxy      Req/s       Avg(ms)   P99(ms)   CPU%   Mem(MB)  Errors
------------------------------------------------------------------------
titan      142,341     0.68      1.89      42     18       0        🏆
nginx      125,432     0.79      2.34      45     23       0
haproxy    118,234     0.84      2.67      48     21       0
envoy       89,123     1.12      4.23      67     45       0

📊 Analysis:
  • titan is 13.5% faster than nginx (2nd place)
  • titan has 19.3% lower P99 latency than nginx
  • titan uses 6.7% less CPU than nginx
```

## Full Test Suite

```bash
# Run everything (HTTP/1.1 + HTTP/2, ~30 minutes)
./scripts/run-all.sh
```

## Individual Tests

### HTTP/1.1 Scenarios
```bash
./scripts/run-http1.sh small            # 1KB responses (latency-focused)
./scripts/run-http1.sh medium           # 10KB responses (balanced)
./scripts/run-http1.sh large            # 100KB responses (throughput)
./scripts/run-http1.sh high-concurrency # 2000 connections
./scripts/run-http1.sh keepalive        # Connection reuse
```

### HTTP/2 Scenarios
```bash
./scripts/run-http2.sh small            # 1KB with multiplexing
./scripts/run-http2.sh medium           # 10KB with multiplexing
./scripts/run-http2.sh high-streams     # 1000 concurrent streams
```

## Comparing Results

```bash
# Compare specific scenario
./scripts/compare.py results/http1-small-*.json

# Compare all HTTP/1.1
./scripts/compare.py results/http1-*.json

# Compare everything
./scripts/compare.py results/*.json
```

## Troubleshooting

**Port in use:**
```bash
pkill -9 nginx haproxy envoy titan
```

**Backend not responding:**
```bash
pkill -9 uvicorn
cd /workspace/tests/mock-backend && python3 main.py &
```

**Quick sanity check:**
```bash
curl http://localhost:3001/small    # Backend
curl http://localhost:8080/small    # Proxy (after starting)
```

## What Gets Tested

**Proxies:**
- ✅ Nginx (industry standard)
- ✅ HAProxy (high-performance LB)
- ✅ Envoy (cloud-native)
- ✅ Titan (our gateway)
- ⊘ Kong (optional, slow to install)

**Metrics:**
- Throughput (req/s)
- Latency (avg, p50, p99)
- CPU usage
- Memory footprint
- Error rate

## Expected Results (ARM64 container)

**HTTP/1.1 Small (1KB):**
- Titan: ~130-150k req/s, P99 <2ms
- Nginx: ~110-130k req/s, P99 ~2.5ms
- HAProxy: ~100-120k req/s, P99 ~3ms
- Envoy: ~80-100k req/s, P99 ~4ms

**HTTP/2 Small (1KB):**
- Titan: ~80-100k req/s
- Nginx: ~70-90k req/s
- HAProxy: ~60-80k req/s
- Envoy: ~50-70k req/s

## Next Steps

- See [README.md](README.md) for full documentation
- See [configs/](configs/) for proxy configurations
- See [scripts/](scripts/) for implementation details
