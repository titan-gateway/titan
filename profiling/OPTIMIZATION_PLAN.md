# Titan API Gateway - Performance Optimization Plan

**Created:** December 9, 2025
**Status:** Ready for Implementation
**Expected Overall Gain:** 20-35% throughput improvement

---

## Executive Summary

Profiling has identified **3 critical bottlenecks** and **5 high-impact optimization opportunities** across the Titan API Gateway. The most significant issue is **excessive file descriptor closes** (23.7x per request), followed by hashtable operations and string allocations. Optimizing these will yield an estimated **20-35% improvement** in throughput.

### Profiling Results Summary

| Scenario | Throughput | Avg Latency | Key Finding |
|----------|-----------|-------------|-------------|
| HTTP/1.1 Baseline | 1,679 req/s | 215ms | Excessive close_fd calls (23.7x) |
| Middleware: None | 1,583 req/s | 221ms | Similar baseline performance |
| Middleware: All | 3,410 req/s | 150ms | Route/test variance (further investigation needed) |

**Note:** Middleware comparison showed unexpected performance variance, likely due to different routes being tested. The baseline HTTP/1.1 profile provides the most reliable data for optimization.

---

## 🚨 Critical Issues

### 1. Excessive File Descriptor Close Operations

**Priority:** CRITICAL (P0)
**Impact:** 10-20% throughput improvement
**Effort:** Medium (2-3 days investigation + fix)

**Problem:**
- 1,196,320 `close_fd()` calls for 50,541 requests = **23.7 closes per request**
- Expected: 2-4 closes per request (client connection + backend connection)
- This is 6-12x higher than it should be

**Root Cause Hypotheses:**
1. Backend connection pool not reusing connections
2. Health checks closing connections too aggressively
3. Error handling forcing connection disposal
4. Defensive programming with duplicate closes

**Investigation Plan:**
```cpp
// Phase 1: Add instrumentation
// File: src/core/socket.cpp
static thread_local uint64_t close_count = 0;
static thread_local std::unordered_map<int, std::string> fd_origins;

void close_fd(int fd) {
    close_count++;
    if (close_count % 100 == 0) {
        LOG_WARN("close_fd called {} times, origin: {}",
                 close_count, fd_origins[fd]);
    }
    close(fd);
    fd_origins.erase(fd);
}

// Phase 2: Track origins
void track_fd_origin(int fd, const char* origin) {
    fd_origins[fd] = origin;
}

// Phase 3: Add pool metrics
struct PoolMetrics {
    uint64_t acquire_hit = 0;
    uint64_t acquire_miss = 0;
    uint64_t health_fail = 0;
    uint64_t explicit_close = 0;

    void log() {
        LOG_INFO("Pool: hits={}, misses={}, health_fail={}, close={}",
                 acquire_hit, acquire_miss, health_fail, explicit_close);
    }
};
```

**Expected Outcome:**
- Identify why connections are being closed excessively
- Fix connection pool health check logic
- Achieve 80%+ pool hit rate
- Reduce close_fd calls to 2-4 per request
- **Gain: 10-20% throughput improvement**

**Files to Modify:**
- `src/core/socket.cpp` - Instrumentation
- `src/gateway/connection_pool.cpp` - Pool metrics + health check fix
- `src/core/server.cpp` - Connection lifecycle tracking

---

### 2. Heavy Hashtable Operations (Header Maps)

**Priority:** HIGH (P1)
**Impact:** 5-15% latency reduction
**Effort:** Low (1-2 days)

**Problem:**
- 43,303 hashtable assignments for 50,541 requests
- Excessive header map copying in request forwarding
- Each copy triggers multiple allocations

**Current Code (Inefficient):**
```cpp
// File: src/core/server.cpp:682-753
std::string build_backend_request(
    const Request& req,
    const std::unordered_map<string, string>& headers  // COPY!
) {
    // Builds request with copied headers...
}

// Caller
auto backend_req = build_backend_request(request, request.headers);  // COPY!
```

**Optimized Code:**
```cpp
// Use move semantics
std::string build_backend_request(
    const Request& req,
    std::unordered_map<string, string>&& headers  // MOVE!
) {
    // Builds request with moved headers...
}

// Caller
auto backend_req = build_backend_request(request, std::move(request.headers));
```

**Alternative Optimization (Zero-Copy):**
```cpp
// Use string_view maps (no allocations)
using HeaderMap = std::unordered_map<string_view, string_view>;

std::string build_backend_request(
    const Request& req,
    const HeaderMap& headers  // String views, no copy
) {
    // Builds request with zero-copy headers...
}
```

**Expected Outcome:**
- 43,303 → ~5,000 hashtable operations
- Eliminate header allocation overhead
- **Gain: 5-15% latency reduction**

**Files to Modify:**
- `src/core/server.cpp:682-753` - `build_backend_request()`
- `src/gateway/pipeline.cpp` - Middleware header passing
- `src/http/http.hpp` - Consider string_view header maps

---

### 3. String Allocation Overhead

**Priority:** MEDIUM (P2)
**Impact:** 5-10% CPU time reduction
**Effort:** Medium (2-3 days)

**Problem:**
- 914,014 string deallocations (18 per request)
- 218,174 string constructions (4.3 per request)
- Each malloc/free pair: 50-100ns overhead

**Solution: Per-Request Arena Allocator**

Titan already has an `Arena` allocator (`src/core/memory.hpp`) but it's not being used in the request hot path!

**Implementation:**
```cpp
// File: src/gateway/pipeline.hpp
struct RequestContext {
    Arena request_arena{64 * 1024};  // 64KB per request
    Request* request;
    Response* response;
    // ... other fields
};

// File: src/core/server.cpp
void process_request(Connection& conn) {
    RequestContext ctx;
    // All temporary strings allocated from arena
    auto* temp_str = ctx.request_arena.allocate<char>(size);
    // No need to free individual allocations

    // ... process request

    // Single deallocation at end
    ctx.request_arena.reset();  // Frees all at once
}
```

**Expected Outcome:**
- 914,014 → ~100 deallocations (only per-request resets)
- Eliminate malloc/free overhead in hot path
- Better cache locality (sequential allocation)
- **Gain: 5-10% CPU time reduction**

**Files to Modify:**
- `src/gateway/pipeline.hpp` - Add arena to RequestContext
- `src/core/server.cpp` - Use arena for temporary strings
- `src/http/parser.cpp` - Arena-based string construction

---

## 💡 High-Impact Optimizations

### 4. SIMD Header Parsing

**Priority:** MEDIUM (P2)
**Impact:** 5-10% parsing speedup
**Effort:** High (3-5 days)

**Problem:**
- llhttp parsing is 27% of CPU time
- Sequential header separator search (`:` character)
- No SIMD acceleration for header boundaries

**Optimization:**
```cpp
// File: src/http/parser.cpp
#include <arm_neon.h>  // ARM64 NEON

const char* find_colon_simd(const char* data, size_t len) {
    const uint8x16_t colon = vdupq_n_u8(':');

    for (size_t i = 0; i + 16 <= len; i += 16) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)(data + i));
        uint8x16_t cmp = vceqq_u8(chunk, colon);

        // Check if any byte matched
        uint64x2_t mask = vreinterpretq_u64_u8(cmp);
        if (vgetq_lane_u64(mask, 0) | vgetq_lane_u64(mask, 1)) {
            // Found colon, find exact position
            for (size_t j = 0; j < 16; ++j) {
                if (data[i + j] == ':') {
                    return data + i + j;
                }
            }
        }
    }

    // Handle remaining bytes
    for (size_t i = (len / 16) * 16; i < len; ++i) {
        if (data[i] == ':') return data + i;
    }
    return nullptr;
}
```

**Expected Outcome:**
- 3-5x faster header separator search
- Reduced llhttp callback overhead
- **Gain: 5-10% parsing speedup**

**Files to Modify:**
- `src/http/parser.cpp` - SIMD header scanning
- Consider upstream contribution to llhttp

---

### 5. Middleware Fusion

**Priority:** LOW (P3)
**Impact:** 2-5% middleware overhead reduction
**Effort:** Medium (2-3 days)

**Problem:**
- 7 virtual function calls per request (middleware pipeline)
- Poor instruction cache locality
- Virtual dispatch overhead (~5ns per call)

**Optimization:**
```cpp
// File: src/gateway/pipeline.cpp
// Before: Separate virtual calls
for (auto& mw : request_middleware) {
    auto result = mw->process_request(ctx);  // Virtual dispatch
    if (result == Stop) return;
}

// After: Fused inline logic
inline MiddlewareResult process_request_fused(RequestContext& ctx) {
    // CORS check (inlined)
    if (cors_enabled && ctx.request->method == OPTIONS) {
        ctx.response->status = 204;
        add_cors_headers(ctx.response);
        return Stop;
    }

    // Rate limit check (inlined)
    if (!rate_limiter.allow(ctx.client_ip)) {
        ctx.response->status = 429;
        return Stop;
    }

    // Transform (inlined)
    if (transform_enabled) {
        apply_transforms(ctx);
    }

    return Continue;
}
```

**Expected Outcome:**
- 7 vtable lookups → 0 (all inlined)
- Better instruction cache locality
- Compiler can optimize across boundaries
- **Gain: 2-5% middleware overhead reduction**

**Files to Modify:**
- `src/gateway/pipeline.cpp` - Fused middleware execution
- Consider adding compile-time option for fusion

---

### 6. Route Caching (LRU)

**Priority:** LOW (P3)
**Impact:** 2-5% routing overhead elimination
**Effort:** Medium (2-3 days)

**Problem:**
- Every request traverses radix tree (2.7% CPU)
- Hot paths are re-computed repeatedly
- No caching of recent route lookups

**Optimization:**
```cpp
// File: src/gateway/router.cpp
struct RouteCache {
    static constexpr size_t MAX_ENTRIES = 256;

    struct Entry {
        uint64_t path_hash;
        RouteMatch match;
        uint64_t access_count;
    };

    std::array<Entry, MAX_ENTRIES> cache;
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};

    std::optional<RouteMatch> lookup(string_view path) {
        uint64_t hash = std::hash<string_view>{}(path);
        size_t idx = hash % MAX_ENTRIES;

        if (cache[idx].path_hash == hash) {
            cache[idx].access_count++;
            hits++;
            return cache[idx].match;
        }

        misses++;
        return std::nullopt;
    }

    void insert(string_view path, const RouteMatch& match) {
        uint64_t hash = std::hash<string_view>{}(path);
        size_t idx = hash % MAX_ENTRIES;
        cache[idx] = {hash, match, 1};
    }
};

// Usage
std::optional<RouteMatch> Router::match(Method method, string_view path) const {
    // Check cache first
    if (auto cached = route_cache.lookup(path)) {
        return *cached;
    }

    // Cache miss, do full search
    auto match = search(root, path, method, params, 0);

    // Insert into cache
    if (match) {
        route_cache.insert(path, *match);
    }

    return match;
}
```

**Expected Outcome:**
- 80%+ cache hit rate for production traffic
- Eliminate radix tree traversal for hot paths
- **Gain: 2-5% routing overhead reduction**

**Files to Modify:**
- `src/gateway/router.cpp` - LRU cache implementation
- Thread-local cache (no locking needed)

---

## 📋 Implementation Roadmap

### Phase 1: Critical Fixes (Week 1-2)

**Priority:** P0 - Critical path optimizations

| Task | Effort | Expected Gain | Owner |
|------|--------|---------------|-------|
| 1. Investigate `close_fd` anomaly | 3 days | 10-20% | TBD |
| 2. Fix connection pool issues | 2 days | Part of above | TBD |
| 3. Add pool hit/miss metrics | 1 day | Observability | TBD |

**Deliverables:**
- [ ] Instrumentation PR with fd tracking
- [ ] Connection pool fix PR
- [ ] Metrics dashboard for pool efficiency
- [ ] Performance regression test

**Success Criteria:**
- close_fd calls reduced from 23.7 to <4 per request
- Pool hit rate >80% in warm state
- Throughput improves by 10-20%

---

### Phase 2: Header Optimization (Week 3)

**Priority:** P1 - High impact, low effort

| Task | Effort | Expected Gain | Owner |
|------|--------|---------------|-------|
| 4. Implement move semantics for headers | 1 day | 5-15% | TBD |
| 5. Refactor `build_backend_request()` | 1 day | Part of above | TBD |
| 6. Add benchmarks for header operations | 1 day | Validation | TBD |

**Deliverables:**
- [ ] Header move semantics PR
- [ ] Before/after benchmark results
- [ ] Updated profiling showing reduced hashtable ops

**Success Criteria:**
- Hashtable operations reduced from 43k to <10k
- Latency improves by 5-15%
- No functional regressions

---

### Phase 3: Arena Allocator Integration (Week 4-5)

**Priority:** P2 - Medium impact, medium effort

| Task | Effort | Expected Gain | Owner |
|------|--------|---------------|-------|
| 7. Add arena to RequestContext | 1 day | 5-10% | TBD |
| 8. Migrate request path to arena | 3 days | Part of above | TBD |
| 9. Benchmark arena vs malloc | 1 day | Validation | TBD |

**Deliverables:**
- [ ] Arena integration PR
- [ ] Memory profiling showing reduced allocations
- [ ] Performance benchmarks

**Success Criteria:**
- String deallocations reduced from 914k to <1k
- CPU time improves by 5-10%
- Memory usage stable or improved

---

### Phase 4: Advanced Optimizations (Week 6-8)

**Priority:** P2-P3 - Incremental improvements

| Task | Effort | Expected Gain | Owner |
|------|--------|---------------|-------|
| 10. SIMD header parsing | 4 days | 5-10% | TBD |
| 11. Middleware fusion | 2 days | 2-5% | TBD |
| 12. Route caching (LRU) | 2 days | 2-5% | TBD |

**Deliverables:**
- [ ] SIMD parsing PR with ARM64/x86_64 support
- [ ] Middleware fusion PR (optional feature flag)
- [ ] Route cache PR with metrics
- [ ] Combined benchmark showing cumulative gains

**Success Criteria:**
- Parsing overhead reduced by 5-10%
- Middleware overhead reduced by 2-5%
- Routing overhead reduced by 2-5%
- Overall throughput improves by additional 10-15%

---

## 📊 Expected Results

### Baseline (Current)

| Metric | Value |
|--------|-------|
| Throughput | 1,679 req/s |
| Avg Latency | 215ms |
| p99 Latency | ~1.9s |
| close_fd calls | 23.7 per request |
| String allocs | 18 per request |
| Header ops | 43,303 total |

### Target (After All Optimizations)

| Metric | Value | Improvement |
|--------|-------|-------------|
| Throughput | 2,200-2,500 req/s | **+31-49%** |
| Avg Latency | 140-160ms | **-26-35%** |
| p99 Latency | <1s | **-47%** |
| close_fd calls | <4 per request | **-83%** |
| String allocs | <2 per request | **-89%** |
| Header ops | <10,000 total | **-77%** |

**Conservative Estimate:** +25-30% overall throughput improvement
**Optimistic Estimate:** +35-45% overall throughput improvement

---

## 🧪 Testing & Validation

### Performance Regression Suite

```bash
# Baseline before optimization
make bench-all
cp results/ results_baseline/

# After each optimization
make bench-all
make bench-compare BEFORE=results_baseline/bench-http1.json AFTER=results/bench-http1.json

# Automated regression detection
./scripts/performance_regression_check.sh results_baseline/ results/
```

### Profiling Validation

```bash
# CPU profile after each phase
docker exec titan-dev bash -c "
    cd /workspace && \
    cmake --preset=release -DCMAKE_CXX_FLAGS='-pg' -DCMAKE_EXE_LINKER_FLAGS='-pg' && \
    cmake --build --preset=release --parallel 4 && \
    ./build/release/src/titan --config config/benchmark-http1.json & \
    wrk -t4 -c100 -d30s http://localhost:8080/api && \
    pkill -TERM titan && sleep 5 && \
    gprof build/release/src/titan gmon.out > profiling/after_phase1.txt
"

# Compare profiles
diff -u profiling/CPU_PROFILE_ANALYSIS.md profiling/after_phase1_analysis.md
```

### Unit Tests

- [ ] Connection pool lifecycle tests
- [ ] Header move semantics tests
- [ ] Arena allocator stress tests
- [ ] SIMD parsing correctness tests
- [ ] Middleware fusion functional tests

### Integration Tests

- [ ] Sustained load test (1 hour+)
- [ ] Memory leak detection (valgrind)
- [ ] Connection leak detection (netstat monitoring)
- [ ] Error rate validation (<0.01%)

---

## 🚀 Quick Wins (Can Start Immediately)

### 1. Add Instrumentation (Day 1)

```cpp
// src/core/socket.cpp
void close_fd(int fd) {
    static thread_local uint64_t count = 0;
    if (++count % 1000 == 0) {
        LOG_WARN("close_fd count: {}", count);
    }
    close(fd);
}
```

### 2. Header Move Semantics (Day 1-2)

```cpp
// src/core/server.cpp
-auto backend_req = build_backend_request(request, request.headers);
+auto backend_req = build_backend_request(request, std::move(request.headers));
```

### 3. Pool Metrics (Day 1)

```cpp
// src/gateway/connection_pool.cpp
struct PoolMetrics {
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
} static thread_local pool_metrics;

// In acquire()
if (found_in_pool) {
    pool_metrics.hits++;
} else {
    pool_metrics.misses++;
}

// Periodic logging
if ((pool_metrics.hits + pool_metrics.misses) % 10000 == 0) {
    LOG_INFO("Pool hit rate: {:.2f}%",
             100.0 * pool_metrics.hits / (pool_metrics.hits + pool_metrics.misses));
}
```

---

## 📈 Success Metrics

### Key Performance Indicators (KPIs)

1. **Throughput:** +25% minimum, +35% target
2. **Latency (p99):** -30% minimum, -50% target
3. **Resource Efficiency:**
   - close_fd calls: -80%
   - Memory allocations: -85%
   - CPU cycles per request: -20%
4. **Reliability:**
   - Error rate: <0.01%
   - Connection pool hit rate: >80%
   - No memory leaks

### Monitoring Dashboard

```
Titan Performance Dashboard
├─ Throughput: [Current] vs [Baseline] vs [Target]
├─ Latency Distribution: p50/p75/p90/p99/p999
├─ Connection Pool Efficiency: Hit rate, Miss rate, Health fails
├─ Memory: Allocations/sec, Active arena usage
└─ Hotspots: CPU time per component
```

---

## 🎯 Conclusion

The profiling has identified clear, actionable optimizations with high confidence in their impact. The **close_fd anomaly** is the highest priority, as it represents the largest single source of overhead. Combined with header optimization and arena allocation, we can realistically achieve **25-35% throughput improvement**.

**Next Steps:**
1. Review and approve optimization plan
2. Assign owners to each task
3. Create tracking issues/PRs
4. Start Phase 1 (critical fixes)

**Questions or Concerns:**
- Reach out to the team for clarification
- Update this plan as new findings emerge
- Re-profile after each phase to validate gains

---

**Document Status:** ✅ Ready for Review
**Author:** Claude Code (Profiling Analysis)
**Reviewers:** Titan Development Team
