# Titan API Gateway - Profiling & Benchmarking Guide

This guide covers performance profiling, benchmarking, and optimization workflows for Titan.

## Table of Contents

1. [Quick Start](#quick-start)
2. [Hot Path Analysis](#hot-path-analysis)
3. [CPU Profiling](#cpu-profiling)
4. [Memory Profiling](#memory-profiling)
5. [Benchmarking](#benchmarking)
6. [Optimization Workflow](#optimization-workflow)
7. [Tool Reference](#tool-reference)

---

## Quick Start

**IMPORTANT:** If running inside a Docker container, see [Docker-Specific Instructions](#docker-container-profiling) below.

### Prerequisites

Check that all required tools are installed:

```bash
make check-tools
```

If any tools are missing, the command will provide installation instructions.

### Run a Quick Benchmark

```bash
# 1. Build release binary
make release

# 2. Start mock backend
make setup-backend

# 3. Run HTTP/1.1 baseline benchmark (30 seconds)
make bench-http1

# Results saved to: results/bench-http1.json
```

### Generate CPU Profile

**For native Linux (with perf access):**

```bash
# Generate flamegraph showing CPU hotspots
make profile-cpu-perf

# Open flamegraph
open profiling/flamegraph.svg
```

**For Docker containers or systems without perf:**

```bash
# Use gprof instead (works everywhere)
# See "Docker Container Profiling" section below
```

---

## Hot Path Analysis

Based on comprehensive code analysis, Titan's performance is dominated by these hot paths:

### 1. **Event Loop & Socket I/O** (20-60% latency)
- **Location:** `src/core/server.cpp:176-262`
- **Overhead:** TLS operations dominate HTTPS traffic (~40-60%)
- **Optimization opportunities:**
  - Kernel TLS (kTLS) offload
  - Larger pre-allocated buffers
  - Batch syscalls with `sendmmsg`/`recvmmsg`

### 2. **TLS/SSL Operations** (40-60% for HTTPS)
- **Location:** `src/core/tls.cpp`
- **Overhead:** OpenSSL crypto operations (~10-50μs per read/write)
- **Optimization opportunities:**
  - Enable kTLS for kernel-side crypto
  - Session resumption (already enabled)
  - AES-NI hardware acceleration (already used)

### 3. **Middleware Chain** (2-50% depending on JWT)
- **Location:** `src/gateway/pipeline.hpp`
- **Overhead:** JWT validation dominates when enabled (~50-200μs per request)
- **Optimization opportunities:**
  - JWT key caching (cache `EVP_PKEY` per `kid`)
  - SIMD-accelerated header parsing
  - Middleware fusion (combine multiple passes)

### 4. **Backend Connection Pool** (1-90% on DNS miss)
- **Location:** `src/gateway/connection_pool.cpp`
- **Overhead:** Blocking DNS resolution (~10-50ms when cache misses)
- **Optimization opportunities:**
  - Async DNS resolution (c-ares library)
  - Connection pre-warming
  - DNS cache with TTL expiration

### 5. **HTTP Parsing** (5-10%)
- **Location:** `src/http/parser.cpp`
- **Overhead:** llhttp state machine (~500ns-2μs per request)
- **Note:** Already well-optimized, minimal improvement potential

### 6. **Routing** (3-5%)
- **Location:** `src/gateway/router.cpp`
- **Overhead:** Radix tree traversal with SIMD (~200-500ns)
- **Optimization opportunities:**
  - Route caching for hot paths (LRU cache)
  - Perfect hash for static routes

### 7. **Memory Allocation** (2-5%)
- **Location:** `src/core/memory.hpp`
- **Overhead:** mimalloc allocations (~50-100ns per allocation)
- **Optimization opportunities:**
  - Request-scoped arena allocators
  - Response buffer pooling

**Key Takeaway:** Focus optimization efforts on TLS (kTLS), DNS resolution (async), and JWT (key caching) for maximum impact.

---

## Docker Container Profiling

**Problem:** `perf` doesn't work in Docker containers because it requires kernel tools matching the host kernel version. Docker containers run on the host kernel but lack the matching tools.

**Solution:** Use `gprof` for CPU profiling in Docker. It's compiler-based (doesn't need kernel access) and works everywhere.

### Complete Docker Profiling Workflow

```bash
# All commands run inside the container
docker exec -it titan-dev bash

# Navigate to workspace
cd /workspace

# 1. Build with gprof profiling support
cmake --preset=release \
  -DCMAKE_CXX_FLAGS='-pg -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-pg'
cmake --build --preset=release --parallel 4

# 2. Start mock backend (Python's built-in HTTP server)
python3 -m http.server 3001 --bind 127.0.0.1 > /tmp/backend.log 2>&1 &

# 3. Start Titan with profiling
./build/release/src/titan --config config/benchmark-http1.json > /tmp/titan.log 2>&1 &

# 4. Generate load (wrk is available in container)
wrk -t4 -c100 -d30s http://localhost:8080/api

# 5. Stop Titan gracefully (generates gmon.out)
pkill -TERM titan
sleep 5

# 6. Analyze profiling data
mkdir -p profiling
gprof build/release/src/titan gmon.out > profiling/gprof_analysis.txt

# 7. View top hotspots
head -100 profiling/gprof_analysis.txt
```

### Understanding gprof Output

**Flat Profile (Top section):**
```
  %   cumulative   self              self     total
 time   seconds   seconds    calls  ms/call  ms/call  name
 21.62      0.08     0.08                             llhttp__internal__run
 10.81      0.18     0.04    54588     0.00     0.00  handle_backend_event
```

- **% time:** Percentage of total CPU time spent in this function
- **cumulative seconds:** Total time including this and previous functions
- **self seconds:** Time spent in this function only (excluding callees)
- **calls:** Number of times function was called
- **name:** Function name

**What to look for:**
- Functions with >10% in "% time" column are optimization candidates
- High call counts with low ms/call = many small operations (batching opportunity)
- Low call counts with high ms/call = expensive operations (algorithm optimization)

**Call Graph (Bottom section):**
Shows caller/callee relationships - useful for understanding execution flow.

### Docker Environment Notes

1. **Mock Backend:** Use Python's built-in `http.server` instead of Flask (no dependencies needed)
   ```bash
   python3 -m http.server 3001 --bind 127.0.0.1
   ```

2. **Profiling Overhead:** gprof adds ~5-15% overhead (acceptable for development)

3. **File Location:** `gmon.out` is created in the directory where Titan was started

4. **Multi-threading:** gprof only profiles the main thread by default (worker threads need separate analysis)

5. **Alternative Tools in Docker:**
   - `valgrind --tool=callgrind` (works but very slow, 10-50x overhead)
   - `strace -c` (syscall profiling, minimal overhead)
   - Time-based sampling (manual instrumentation)

### Example Analysis

See `profiling/CPU_PROFILE_ANALYSIS.md` for a complete analysis example with:
- Top 10 CPU hotspots
- Category breakdown (parsing, I/O, middleware, etc.)
- Optimization recommendations
- Comparison with theoretical analysis

---

## CPU Profiling

### Method 1: gperftools (Cross-platform)

**Note:** gperftools (pprof) requires installation and library linking. For Docker containers, use gprof instead (see above).

```bash
# Start profiling (runs for 30 seconds by default)
make profile-cpu

# Analyze text report
pprof --text build/release/src/titan profiling/cpu.prof | head -50

# Generate interactive HTML
pprof --web build/release/src/titan profiling/cpu.prof

# Generate PDF call graph
pprof --pdf build/release/src/titan profiling/cpu.prof > cpu_profile.pdf
```

**Interpreting Results:**

```
Total: 1000 samples
  250  25.0%  25.0%      250  25.0% SSL_read
  150  15.0%  40.0%      150  15.0% llhttp_execute
  100  10.0%  50.0%      100  10.0% Router::match
   80   8.0%  58.0%       80   8.0% send_response
```

- **Column 1:** Samples in this function (flat)
- **Column 2:** % of total samples (flat)
- **Column 3:** Cumulative %
- **Column 4:** Samples including callees (cumulative)
- **Column 5:** % including callees
- **Column 6:** Function name

**What to look for:**
- Functions with >10% flat time are optimization candidates
- Large cumulative vs flat difference indicates expensive callees
- OpenSSL/TLS functions dominating? Consider kTLS
- DNS resolution in profile? Implement async DNS

### Method 2: perf + FlameGraph (Linux only)

```bash
# Generate flamegraph (automatically runs benchmark and profiles)
make profile-cpu-perf

# Open flamegraph in browser
open profiling/flamegraph.svg
```

**Reading Flamegraphs:**

- **X-axis:** Alphabetical order (NOT time!)
- **Y-axis:** Stack depth
- **Width:** CPU time (wider = more expensive)
- **Color:** Random (for visual distinction)

**What to look for:**
- Wide plateaus = CPU hotspots
- Deep stacks = excessive function calls
- Hover for exact percentages

**Example analysis:**

```
[wide plateau]
   ├─ SSL_read (40%)          ← TLS overhead (consider kTLS)
   ├─ llhttp_execute (15%)    ← Parsing (acceptable)
   ├─ Router::match (10%)     ← Routing (acceptable)
   └─ getaddrinfo (8%)        ← DNS blocking (needs async!)
```

---

## Memory Profiling

### Heap Profiling with gperftools

```bash
# Run heap profiling (captures snapshots during execution)
make profile-heap

# Analyze most recent snapshot
pprof --text build/release/src/titan profiling/heap.prof.* | head -50

# Generate heap visualization
pprof --web build/release/src/titan profiling/heap.prof.*
```

**Interpreting Heap Profiles:**

```
Total: 128.5 MB
  40.0  31.1%  31.1%    40.0  31.1% std::vector<char>::reserve
  25.6  19.9%  51.0%    25.6  19.9% Arena::allocate
  15.3  11.9%  62.9%    15.3  11.9% SSL_new
```

**What to look for:**
- Memory leaks (allocations never freed)
- Excessive allocations in hot path
- Fragmentation patterns
- Large temporary buffers

**Common issues:**
- `std::vector` growth → Pre-allocate with `reserve()`
- String concatenation → Use pre-allocated buffers
- SSL object creation → Implement SSL object pooling

### Valgrind (For Memory Leaks)

```bash
# Build with debug symbols
cmake --preset=dev -DCMAKE_BUILD_TYPE=Debug
cmake --build --preset=dev

# Run with Valgrind (slow, 10-50x overhead)
valgrind --leak-check=full --show-leak-kinds=all \
    ./build/dev/src/titan --config config/benchmark-http1.json

# Analyze output for:
# - Definitely lost: Memory leaks (must fix)
# - Indirectly lost: Leaked container contents
# - Possibly lost: Potential leaks (investigate)
# - Still reachable: Global allocations (acceptable)
```

---

## Benchmarking

### Benchmark Scenarios

Titan includes 6 specialized benchmark scenarios to isolate performance factors:

| Scenario | Description | Measures |
|----------|-------------|----------|
| `bench-http1` | HTTP/1.1 cleartext baseline | Core proxy performance without TLS/HTTP2 |
| `bench-http2-tls` | HTTP/2 with TLS | Production HTTPS overhead |
| `bench-jwt` | JWT authentication | Crypto validation overhead |
| `bench-pool` | Multiple backends | Connection pool efficiency |
| `bench-middleware-none` | Zero middleware | Raw proxy (minimal overhead) |
| `bench-middleware-all` | All middleware enabled | Maximum middleware overhead |

### Running Individual Benchmarks

```bash
# HTTP/1.1 baseline (30s, 100 connections, 4 threads)
make bench-http1

# HTTP/2 with TLS (requires TLS certs)
make bench-http2-tls

# JWT authentication overhead
make bench-jwt

# Connection pool stress test
make bench-pool

# Zero middleware (raw proxy)
make bench-middleware-none

# All middleware enabled
make bench-middleware-all
```

### Running All Benchmarks

```bash
# Run all 6 scenarios (takes ~3-4 minutes)
make bench-all

# Generate comprehensive report
make bench-report

# Results:
# - results/bench-*.json (raw data)
# - results/benchmark_report.md (human-readable)
```

### Customizing Benchmark Parameters

```bash
# Longer duration (120 seconds)
make bench-http1 BENCH_DURATION=120

# More connections (500)
make bench-http1 BENCH_CONNECTIONS=500

# More threads (8)
make bench-http1 BENCH_THREADS=8

# All parameters
make bench-http1 BENCH_DURATION=60 BENCH_CONNECTIONS=200 BENCH_THREADS=8
```

### Interpreting Benchmark Results

```json
{
  "scenario": "http1",
  "requests_per_sec": 190423.5,
  "latency": {
    "avg": 0.52,
    "p50": 0.48,
    "p75": 0.61,
    "p90": 0.85,
    "p99": 1.42,
    "max": 8.16
  },
  "success_rate": 100.0
}
```

**Key metrics:**

- **Throughput (req/s):** Higher is better
  - >200k req/s: Excellent (HTTP/1.1 cleartext)
  - >100k req/s: Good (HTTP/2 with TLS)
  - <50k req/s: Investigate bottlenecks

- **p99 Latency (ms):** Lower is better
  - <2ms: Excellent
  - <5ms: Good
  - >10ms: Investigate (likely DNS or TLS overhead)

- **Success Rate (%):** Should be 100%
  - <100%: Connection errors, check logs

**Latency percentiles explained:**

- **p50 (median):** Typical request latency
- **p99:** 99% of requests faster than this
- **p99.9:** Critical for SLA guarantees

**Rule of thumb:** Optimize for p99, not average. Tail latency kills user experience.

---

## Optimization Workflow

### Step 1: Establish Baseline

```bash
# Run all benchmarks to establish baseline
make bench-all

# Save baseline results
mkdir -p results/baseline
cp results/bench-*.json results/baseline/
```

### Step 2: Profile and Identify Bottlenecks

```bash
# Generate CPU flamegraph (Linux)
make profile-cpu-perf

# Or use gperftools (cross-platform)
make profile-cpu
make profile-heap
make analyze-profiles
```

### Step 3: Make Targeted Optimization

Example: Optimize JWT key caching

```cpp
// Before: Loading key on every validation
EVP_PKEY* key = load_public_key(kid);

// After: Cache keys per kid
static thread_local std::unordered_map<std::string, EVP_PKEY*> key_cache;
if (key_cache.find(kid) == key_cache.end()) {
    key_cache[kid] = load_public_key(kid);
}
EVP_PKEY* key = key_cache[kid];
```

### Step 4: Re-benchmark and Compare

```bash
# Run same benchmark
make bench-jwt

# Compare results
make bench-compare \
    BEFORE=results/baseline/bench-jwt.json \
    AFTER=results/bench-jwt.json
```

**Example output:**

```
================================================================================
Benchmark Comparison: jwt
================================================================================

Before: 2025-12-09T10:00:00
After:  2025-12-09T11:30:00

Throughput:
  Requests/sec:     85,234.12 →   105,678.45  ↑ +23.98%

Latency:
  Average           1.17 → 0.95 ms  ↓ -18.80%
  p99               4.52 → 2.31 ms  ↓ -48.89%

================================================================================
✅ Performance improved!
   • Throughput increased by 23.98%
   • p99 latency decreased by 48.89%
================================================================================
```

### Step 5: Validate No Regressions

```bash
# Run full benchmark suite
make bench-all

# Compare all scenarios
for scenario in http1 http2-tls jwt pool middleware-none middleware-all; do
    make bench-compare \
        BEFORE=results/baseline/bench-$scenario.json \
        AFTER=results/bench-$scenario.json
done
```

### Step 6: Document and Commit

```bash
# Add benchmark results to commit
git add results/
git commit -m "perf: improve JWT key caching (+24% throughput, -49% p99 latency)"
```

---

## Tool Reference

### Make Targets

#### Profiling

| Command | Description |
|---------|-------------|
| `make check-tools` | Verify profiling tools are installed |
| `make profile-cpu` | CPU profiling with gperftools (30s) |
| `make profile-cpu-perf` | CPU profiling with perf + flamegraph (Linux only) |
| `make profile-heap` | Heap profiling with gperftools |
| `make analyze-profiles` | Generate summary reports from profiles |
| `make clean-profiles` | Remove all profiling and benchmark data |

#### Benchmarking

| Command | Description |
|---------|-------------|
| `make bench-http1` | HTTP/1.1 cleartext baseline |
| `make bench-http2-tls` | HTTP/2 with TLS |
| `make bench-jwt` | JWT authentication overhead |
| `make bench-pool` | Connection pool stress test |
| `make bench-middleware-none` | Zero middleware (raw proxy) |
| `make bench-middleware-all` | All middleware enabled |
| `make bench-all` | Run all 6 scenarios |
| `make bench-report` | Generate comprehensive report |
| `make bench-compare` | Compare before/after results |

#### Backend

| Command | Description |
|---------|-------------|
| `make setup-backend` | Start mock backend on port 3001 |

### Benchmark Parameters

All benchmark targets accept these parameters:

- **`BENCH_DURATION`:** Duration in seconds (default: 30)
- **`BENCH_CONNECTIONS`:** Number of connections (default: 100)
- **`BENCH_THREADS`:** Number of threads (default: 4)
- **`BENCH_HOST`:** Target host (default: http://localhost:8080)
- **`BENCH_PATH`:** Request path (default: /api)

Example:

```bash
make bench-http1 BENCH_DURATION=60 BENCH_CONNECTIONS=200 BENCH_THREADS=8
```

### Scripts

#### `scripts/benchmark_runner.py`

Orchestrates benchmarks with wrk/h2load and generates reports.

**Usage:**

```bash
# Run specific scenario
./scripts/benchmark_runner.py \
    --scenario http1 \
    --config config/benchmark-http1.json \
    --duration 30 \
    --connections 100 \
    --threads 4 \
    --output results/bench-http1.json

# Generate report from results directory
./scripts/benchmark_runner.py --report results/
```

#### `scripts/compare_results.py`

Compares benchmark results with color-coded improvements.

**Usage:**

```bash
./scripts/compare_results.py \
    results/before.json \
    results/after.json
```

#### `scripts/generate_flamegraph.sh`

Generates CPU flamegraph using perf (Linux only).

**Usage:**

```bash
# Default (30s duration, 100 connections, 4 threads)
./scripts/generate_flamegraph.sh

# Custom parameters
./scripts/generate_flamegraph.sh 60 200 8
```

#### `scripts/check_tools.sh`

Validates profiling/benchmarking tools are installed.

**Usage:**

```bash
# Check all tools
./scripts/check_tools.sh

# Check specific tool
./scripts/check_tools.sh perf
./scripts/check_tools.sh pprof
./scripts/check_tools.sh wrk
./scripts/check_tools.sh h2load
```

---

## Best Practices

### 1. Always Establish Baseline

Before any optimization, run:

```bash
make bench-all
mkdir -p results/baseline
cp results/*.json results/baseline/
```

### 2. Profile Before Optimizing

**Don't guess, measure!**

```bash
# Generate flamegraph to identify hotspots
make profile-cpu-perf
```

### 3. Optimize One Thing at a Time

Each optimization should be:
- Measurable (before/after benchmarks)
- Isolated (single change per commit)
- Documented (commit message with metrics)

### 4. Focus on High-Impact Changes

**Priority by impact:**

1. **Critical (>20% improvement):**
   - kTLS implementation
   - Async DNS resolution
   - JWT key caching

2. **High (10-20% improvement):**
   - Response buffer pooling
   - Route caching
   - SIMD header parsing

3. **Medium (5-10% improvement):**
   - Connection pre-warming
   - TLS session persistence
   - Middleware fusion

4. **Low (<5% improvement):**
   - Micro-optimizations
   - Compiler flags tweaking

### 5. Watch for Regressions

Run full benchmark suite after each change:

```bash
make bench-all
# Compare all scenarios against baseline
```

### 6. Profile in Release Mode

Always profile with optimizations enabled:

```bash
# Profiling uses release build by default
make PRESET=release profile-cpu
```

Debug builds are 5-10x slower and give misleading results.

### 7. Test with Realistic Load

Match production traffic patterns:

- **Connections:** Similar to expected concurrent clients
- **Duration:** At least 60s for stable results
- **Request mix:** Use production path distribution

### 8. Document Improvements

Include metrics in commit messages:

```bash
git commit -m "perf: implement kTLS offload

- Throughput: +45% (190k → 276k req/s)
- p99 latency: -38% (1.42ms → 0.88ms)
- CPU usage: -22% (TLS crypto moved to kernel)

Benchmark: make bench-http2-tls (60s, 200 conn, 8 threads)"
```

---

## Troubleshooting

### Issue: Benchmarks fail to start

**Symptoms:**
```
✗ Titan failed to start
```

**Solutions:**
1. Check if Titan is already running: `pkill titan`
2. Verify config file exists: `ls config/benchmark-http1.json`
3. Build release binary: `make release`
4. Check logs for errors

### Issue: Low throughput (<10k req/s)

**Possible causes:**
1. Backend not started: `make setup-backend`
2. DNS resolution blocking: Check for `getaddrinfo` in flamegraph
3. Connection pool exhausted: Increase `pool_size` in config
4. Port exhaustion: `sysctl net.ipv4.ip_local_port_range`

### Issue: High latency (p99 >100ms)

**Possible causes:**
1. DNS misses: Implement async DNS
2. TLS handshakes: Check session resumption enabled
3. Backend slow: Test backend directly with `curl`
4. Memory allocation: Profile with `make profile-heap`

### Issue: Flamegraph not generated (Linux)

**Symptoms:**
```
✗ perf not found
```

**Solutions:**
1. Install perf: `sudo apt-get install linux-tools-generic`
2. Enable perf for non-root: `echo 0 | sudo tee /proc/sys/kernel/perf_event_paranoid`
3. Clone FlameGraph: `git clone https://github.com/brendangregg/FlameGraph tools/flamegraph`

### Issue: Permission denied errors

**Symptoms:**
```
Permission denied: /proc/sys/kernel/perf_event_paranoid
```

**Solutions:**
```bash
# Allow perf for all users (temporary)
echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid

# Or run as root
sudo make profile-cpu-perf
```

---

## Advanced Topics

### Continuous Performance Monitoring

Set up automated benchmarking in CI/CD:

```yaml
# .github/workflows/benchmark.yml
name: Performance Benchmark
on: [pull_request]

jobs:
  benchmark:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: make release
      - name: Benchmark
        run: make bench-all BENCH_DURATION=60
      - name: Compare with main
        run: |
          # Fetch baseline from main branch
          # Compare results
          # Fail if >5% regression
```

### Kernel Tuning for Maximum Performance

```bash
# Increase file descriptor limits
ulimit -n 1000000

# Increase ephemeral port range
sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# Enable TCP fast open
sysctl -w net.ipv4.tcp_fastopen=3

# Increase TCP buffer sizes
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.wmem_max=134217728
sysctl -w net.ipv4.tcp_rmem="4096 87380 67108864"
sysctl -w net.ipv4.tcp_wmem="4096 65536 67108864"

# Increase connection backlog
sysctl -w net.core.somaxconn=4096
sysctl -w net.ipv4.tcp_max_syn_backlog=8192
```

### Multi-Machine Benchmarking

For testing at scale (>1M req/s):

```bash
# Load generator machine 1
wrk -t8 -c500 -d60s http://titan-host:8080/api > results1.txt

# Load generator machine 2
wrk -t8 -c500 -d60s http://titan-host:8080/api > results2.txt

# Aggregate results
./scripts/aggregate_results.py results1.txt results2.txt
```

---

## References

- [Brendan Gregg's Performance Methodologies](https://www.brendangregg.com/methodology.html)
- [FlameGraph Documentation](https://github.com/brendangregg/FlameGraph)
- [Google's Profiling Best Practices](https://github.com/google/pprof)
- [wrk HTTP Benchmarking Tool](https://github.com/wg/wrk)
- [h2load HTTP/2 Benchmarking](https://nghttp2.org/documentation/h2load.1.html)

---

**Happy Profiling!** 🚀

For questions or issues, please open an issue on the [Titan GitHub repository](https://github.com/titan-gateway/titan).
