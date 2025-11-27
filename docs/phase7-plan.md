# Phase 7: Optimization & Benchmarking - Execution Plan

## Task 1: Remove io_uring Driver Infrastructure

### Files to Delete:
- `src/core/io/driver.hpp` - Abstract IoDriver interface
- `src/core/io/stub.cpp` - Stub implementation
- `src/core/io/` - Remove entire directory
- `benchmarks/baseline-uring.json`
- `benchmarks/poll_vs_uring.md`

### Files to Modify:
1. `src/core/core.hpp` - Remove IoDriver, WorkerConfig, Context class
2. `src/core/core.cpp` - Remove Context implementation, keep thread utilities
3. `src/control/config.hpp` - Remove io_ring_entries, use_sqpoll fields
4. `tests/unit/test_core.cpp` - Remove Context/IoDriver tests
5. `src/CMakeLists.txt` - Remove io/ files from build
6. `CLAUDE.md` - Remove io_uring from tech stack
7. `README.md` - Update architecture description
8. `docker-compose.yml` - Remove SYS_NICE, IPC_LOCK capabilities

---

## Task 2: Phase 7 Optimization Roadmap

### 7.1 Establish Baseline Performance (Week 1, Days 1-2)

**Objective:** Measure current performance

**Activities:**
1. Setup benchmarking infrastructure (wrk2, h2load)
2. Benchmark HTTP/1.1 (small/medium/large responses)
3. Benchmark HTTP/2 (multiplexing, ALPN overhead)
4. Benchmark HTTPS (TLS handshake, encrypted throughput)

**Deliverables:**
- `benchmarks/baseline-poll.md` - Performance metrics
- `benchmarks/benchmark.sh` - Automated script
- Dashboard: RPS, latency percentiles, throughput

---

### 7.2 Profile Hot Paths (Week 1, Days 3-4)

**Objective:** Identify CPU bottlenecks

**Activities:**
1. Setup profiling (perf, FlameGraph tools)
2. CPU profiling (flamegraphs, top 10 hot functions)
3. Memory profiling (allocation hotspots)
4. Cache profiling (L1/L2/L3 efficiency, branch prediction)

**Deliverables:**
- `benchmarks/flamegraph-baseline.svg` - CPU flamegraph
- `benchmarks/hotspots.md` - Top 10 bottlenecks
- `benchmarks/memory-profile.txt` - Allocation analysis

---

### 7.3 Low-Hanging Fruit Optimizations (Week 1, Days 5-7)

**Objective:** Fix obvious inefficiencies

**Activities:**
1. Memory allocation optimizations (reduce vector reallocations, pre-allocate buffers)
2. Connection reuse optimizations (pool warmup, better validation)
3. HTTP parsing optimizations (reduce callback overhead, faster header lookup)
4. Response building optimizations (pre-format common responses)

**Deliverables:**
- Optimized code with comments
- Before/after benchmarks
- Updated unit tests

---

### 7.4 Nginx Baseline Comparison (Week 2, Days 1-2)

**Objective:** Compare against Nginx

**Activities:**
1. Setup Nginx test environment (Docker, same config)
2. Run identical benchmark suite
3. Comparative analysis (RPS, latency, memory, CPU)

**Deliverables:**
- `benchmarks/nginx-comparison.md` - Side-by-side metrics
- Performance graphs
- Strengths/weaknesses analysis

---

### 7.5 Advanced Optimizations (Week 2, Days 3-5) [Optional]

**Objective:** SIMD and algorithmic optimizations

**Activities:**
1. SIMD optimizations (SSE4.2/AVX2 for string scanning)
2. Routing optimizations (radix tree compaction, perfect hashing)
3. Lock-free optimizations (atomic memory ordering, reduce false sharing)

**Deliverables:**
- SIMD-optimized functions
- >10% performance improvement
- Documentation

---

### 7.6 Integration Test Suite (Week 2, Day 6)

**Objective:** Comprehensive testing

**Activities:**
1. Expand pytest integration tests (edge cases, multiplexing, certs)
2. End-to-end scenarios (failover, hot-reload under load)

**Deliverables:**
- `tests/integration/test_e2e.py`
- 150+ tests passing

---

### 7.7 Final Benchmark Report (Week 2, Day 7)

**Objective:** Document improvements

**Deliverables:**
- `benchmarks/phase7-final.md` - Complete report with:
  - Baseline vs final comparison
  - Titan vs Nginx comparison
  - Optimization list
  - Remaining bottlenecks
  - Performance summary

---

## Success Criteria

- ✅ 10-30% throughput improvement over baseline
- ✅ P99 latency < 2ms for small responses
- ✅ Comprehensive performance documentation
- ✅ Nginx comparison showing competitive performance
- ✅ Zero test regressions
- ✅ Production-ready optimization guide

---

## Execution Status

- [ ] Task 1: Remove io_uring infrastructure
- [ ] Task 2.1: Baseline performance
- [ ] Task 2.2: Profile hot paths
- [ ] Task 2.3: Low-hanging optimizations
- [ ] Task 2.4: Nginx comparison
- [ ] Task 2.5: Advanced optimizations
- [ ] Task 2.6: Integration tests
- [ ] Task 2.7: Final report
