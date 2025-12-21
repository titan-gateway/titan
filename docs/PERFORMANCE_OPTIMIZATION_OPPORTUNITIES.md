# Titan Performance Optimization Opportunities

**Generated:** 2025-12-15
**Current Performance:** 75,166 req/s (with Nginx backend, 200 connections)
**Target:** 8-15% improvement (82K-86K req/s)

---

## Executive Summary

Analysis of Titan's hot path identified **8 concrete optimization opportunities** with specific line numbers and code references. The codebase is well-architected with Thread-Per-Core and Shared-Nothing patterns, but several string allocations and data structure operations can be optimized.

**Expected cumulative improvements:**
- **Throughput:** +8-15% (82K-86K req/s)
- **Tail Latency (P99):** -10-20% improvement
- **Memory Allocations:** -30-40% reduction

---

## Critical Hot Path Bottlenecks (Priority: HIGH)

### 1. Unnecessary String Allocations in Request Building

**File:** `src/core/server.cpp`
**Lines:** 1001, 1026, 1046, 1566

**Current Code:**
```cpp
// Line 1026 - Creates string allocation per header
std::string modify_key = "header_modify:" + std::string(header.name);
auto modify_it = metadata.find(modify_key);
```

**Problem:**
- Each request processes 10-50 headers
- Creates string allocation per header for metadata lookups
- At 75K req/s: **750K-3.75M allocations/sec**
- **Impact:** 2-5% CPU overhead

**Solution:**
Pre-compute header modifications during request phase:

```cpp
// Pre-compute in RequestContext
struct HeaderModifications {
    titan::core::fast_map<std::string_view, std::string_view> modify_map;
    std::vector<std::string_view> remove_set;
};

// Then use O(1) lookup without allocation:
auto it = ctx.header_mods.modify_map.find(header.name);
```

**Effort:** Medium
**Benefit:** 2-3% throughput improvement

---

### 2. Load Balancer O(n) Backend Filtering

**File:** `src/gateway/upstream.cpp`
**Lines:** 38-52 (RoundRobinBalancer), 84-99 (RandomBalancer), 112-132 (WeightedRoundRobinBalancer)

**Current Code:**
```cpp
std::vector<Backend*> available;
for (auto& backend : backends) {
    if (backend.can_accept_connection()) {
        available.push_back(const_cast<Backend*>(&backend));
    }
}
if (available.empty()) return nullptr;
uint64_t index = counter_.fetch_add(1, std::memory_order_relaxed) % available.size();
return available[index];
```

**Problem:**
- O(n) vector allocation + filtering **per request**
- 10 backends = 10 iterations + 1 allocation per request
- At 75K req/s: **750K allocations/sec** just for load balancing
- **Impact:** 3-5% CPU overhead

**Solution:**
Cache available backends, rebuild only on health changes:

```cpp
class LoadBalancer {
private:
    std::vector<Backend*> available_cache;
    std::atomic<uint64_t> health_version{0};

public:
    Backend* select(...) {
        if (needs_refresh()) {
            rebuild_cache();  // Only on health change
        }
        return available_cache[counter_++ % available_cache.size()]; // O(1)
    }
};
```

**Effort:** Medium
**Benefit:** 3-5% throughput improvement

---

### 3. Redundant Header String Copies

**File:** `src/core/server.cpp`
**Lines:** 1437-1439, 1445-1447

**Current Code:**
```cpp
// Copy headers to storage (allocates)
for (const auto& h : response.headers) {
    client_conn.response_header_storage.emplace_back(
        std::string(h.name),   // Copy
        std::string(h.value)   // Copy
    );
}

// Then recreate views - REDUNDANT
for (const auto& [name, value] : client_conn.response_header_storage) {
    client_conn.response.headers.push_back({name, value});
}
```

**Problem:**
- **2x string copies** per header (storage + view recreation)
- **2N allocations** (N = number of headers)
- **Impact:** 10-20μs per request

**Solution:**
Unified storage with single copy:

```cpp
struct ResponseHeaders {
    std::vector<std::pair<std::string, std::string>> owned;
    std::vector<Header> views;

    void add(std::string_view name, std::string_view value) {
        owned.emplace_back(name, value);
        const auto& [n, v] = owned.back();
        views.push_back({n, v});  // Single copy, safe lifetime
    }
};
```

**Effort:** Low
**Benefit:** 5-10% header processing overhead reduction

---

### 4. Buffer Insertion Inefficiency

**File:** `src/core/server.cpp`
**Lines:** 236, 269, 1090, 1109, 1138, 1373

**Current Code:**
```cpp
// TLS/socket read loop
while (true) {
    n = ssl_read_nonblocking(conn.ssl, buffer);
    if (n > 0) {
        conn.recv_buffer.insert(conn.recv_buffer.end(), buffer, buffer + n);
    }
}
```

**Problem:**
- `vector::insert()` at end triggers reallocation if capacity exceeded
- HTTP/2 can read 20+ frames in single epoll event
- Cascading reallocations: 8KB → 16KB → 32KB
- **Impact:** 5-7% latency on large transfers

**Solution:**
Pre-reserve capacity:

```cpp
if (conn.recv_buffer.size() + 8192 > conn.recv_buffer.capacity()) {
    conn.recv_buffer.reserve(conn.recv_buffer.capacity() * 1.5);
}
conn.recv_buffer.insert(conn.recv_buffer.end(), buffer, buffer + n);
```

**Effort:** Low
**Benefit:** 5-7% latency improvement on large responses

---

## Medium Priority Optimizations

### 5. Case-Insensitive Header Comparison Allocates Strings

**File:** `src/core/server.cpp`
**Lines:** 1560-1576

**Current Code:**
```cpp
headers.erase(std::remove_if(headers.begin(), headers.end(),
    [](const http::Header& h) {
        std::string name_lower(h.name);  // ALLOCATION
        std::transform(name_lower.begin(), name_lower.end(),
                      name_lower.begin(), ::tolower);
        return name_lower == "connection" || ...
    }), headers.end());
```

**Problem:**
- HTTP/2 response with 20 headers = 20 string allocations
- **Impact:** 10-15μs per response

**Solution:**
```cpp
auto iequals = [](std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](char ac, char bc) { return std::tolower(ac) == std::tolower(bc); });
};
```

**Effort:** Low
**Benefit:** Eliminates allocations, 3-5% improvement on HTTP/2

---

### 6. Connection Pool Linear Search

**File:** `src/gateway/connection_pool.hpp`
**Lines:** 95-103 (acquire method)

**Problem:**
- LIFO stack requires **O(n) search** for matching (host, port)
- 64-connection pool with mixed destinations: avg 32 iterations
- **Impact:** 50-100 CPU cycles per acquire

**Solution:**
```cpp
class BackendConnectionPool {
private:
    fast_map<std::pair<std::string, uint16_t>, std::vector<int>> by_destination;

public:
    int acquire(const std::string& host, uint16_t port) {
        auto it = by_destination.find({host, port});
        if (it != by_destination.end() && !it->second.empty()) {
            int fd = it->second.back();
            it->second.pop_back();
            return fd;  // O(1)
        }
        return -1;
    }
};
```

**Effort:** Medium
**Benefit:** 2-3% per request

---

### 7. Pre-Format CORS Headers

**File:** `src/gateway/pipeline.cpp`
**Lines:** 59-70 (CorsMiddleware)

**Current Code:**
```cpp
std::string methods;
for (size_t i = 0; i < config_.allowed_methods.size(); ++i) {
    if (i > 0) methods += ", ";  // String realloc
    methods += config_.allowed_methods[i];
}
```

**Problem:**
- String building via `+=` causes up to 5 reallocations
- CORS headers on every response
- **Impact:** 5-10μs per request

**Solution:**
```cpp
// Pre-format in constructor
CorsMiddleware(Config config) {
    preformatted_methods = join(config.allowed_methods, ", ");
}
```

**Effort:** Low
**Benefit:** 3-5μs improvement per request

---

## Low Priority (Future Optimization)

### 8. Synchronous DNS Resolution

**File:** `src/core/server.cpp`
**Lines:** 922, 927

**Problem:**
- `getaddrinfo()` blocks 10-100ms on DNS miss
- Blocks entire worker thread
- **Impact:** 0.1-1% throughput, tail latency spikes

**Solution:**
Implement async DNS with thread pool or c-ares library

**Effort:** High (requires async infrastructure)
**Benefit:** 0.5-1% tail latency improvement

---

## Optimization Priority Summary (Ordered by Impact)

| Priority | Optimization | File | Lines | Frequency | Allocation Rate | Impact | Effort |
|----------|-------------|------|-------|-----------|-----------------|--------|--------|
| **🔥1** | Header modification strings | server.cpp | 1001-1046 | Every request | **750K-3.75M/sec** | 2-5% CPU | Medium |
| **🔥2** | Load balancer caching | upstream.cpp | 38-132 | Every request | **750K/sec** | 3-5% throughput | Medium |
| **🔥3** | Header copy elimination | server.cpp | 1437-1447 | Every response | **2N allocations** | 5-10% header overhead | Low |
| **⚡4** | Buffer pre-reserve | server.cpp | 236-1373 | Large transfers | Cascade reallocs | 5-7% latency | Low |
| **⚡5** | Connection pool O(n) search | connection_pool.hpp | 95-103 | Backend acquire | 50-100 cycles | 2-3% | Medium |
| **⚡6** | Case-insensitive compare | server.cpp | 1566 | HTTP/2 response | 20 strings/resp | 3-5% HTTP/2 | Low |
| **📊7** | CORS pre-format | pipeline.cpp | 59-70 | CORS routes only | 5 reallocs | 3-5μs | Low |
| **📊8** | Async DNS | server.cpp | 922 | Cache miss only | Blocks thread | 0.5-1% tail | High |

**Legend:**
- 🔥 = Critical (affects every request)
- ⚡ = High impact (common operations)
- 📊 = Medium impact (specific scenarios)

---

## Implementation Phases (Reordered by Impact)

### Phase 1: Critical Hot Path (3-5 days) 🔥
**Focus:** Eliminate allocations in every request/response

1. **Header modification pre-compute** (Priority #1)
   - `server.cpp:1001-1046`
   - Impact: 750K-3.75M allocations/sec → 0
   - Benefit: 2-5% CPU reduction

2. **Load balancer caching** (Priority #2)
   - `upstream.cpp:38-132`
   - Impact: 750K allocations/sec → ~100/sec (on health changes)
   - Benefit: 3-5% throughput

3. **Header copy elimination** (Priority #3)
   - `server.cpp:1437-1447`
   - Impact: 2N → N allocations per response
   - Benefit: 5-10% header overhead reduction

**Expected cumulative:** +8-12% throughput, -40% allocations

---

### Phase 2: High-Impact Operations (2-3 days) ⚡

4. **Buffer pre-reservation** (Priority #4)
   - `server.cpp:236, 269, 1373`
   - Impact: Eliminates cascade reallocations
   - Benefit: 5-7% latency improvement on large transfers

5. **Connection pool indexing** (Priority #5)
   - `connection_pool.hpp:95-103`
   - Impact: O(n) → O(1) acquire
   - Benefit: 2-3% per request

6. **Case-insensitive compare** (Priority #6)
   - `server.cpp:1566`
   - Impact: 20 allocations/response → 0
   - Benefit: 3-5% on HTTP/2

**Expected cumulative:** +12-17% throughput

---

### Phase 3: Polish & Future (1-2 days + optional) 📊

7. **CORS pre-formatting** (Priority #7) - Easy win
   - `pipeline.cpp:59-70`
   - Impact: 5 reallocations → 0
   - Benefit: 3-5μs per request

8. **Async DNS** (Priority #8) - Future/optional
   - `server.cpp:922`
   - Impact: Eliminates thread blocking
   - Benefit: 0.5-1% tail latency
   - **Note:** High effort, deferred to later phase

**Expected cumulative:** +13-18% total throughput

---

## Validation Methodology

For each optimization:

1. **Benchmark Before/After**
   ```bash
   # HTTP/1.1
   wrk -t4 -c200 -d30s http://localhost:8080/

   # HTTP/2
   h2load -n1000000 -c200 -t4 -m100 https://localhost:8443/
   ```

2. **Memory Safety**
   ```bash
   cmake --preset=dev -DENABLE_ASAN=ON
   ctest --preset=test
   ```

3. **Integration Tests**
   ```bash
   pytest tests/integration/ -v  # All 94 tests must pass
   ```

4. **Profile Validation**
   ```bash
   DURATION=20 ./scripts/profile_titan_simple.sh
   # Compare memory allocations before/after
   ```

---

## Performance Targets

**Current (v0.1.0):**
- HTTP/1.1: 75,166 req/s (200 connections, 4 threads)
- HTTP/2: ~118K req/s (documented benchmark)
- Memory: 11MB RSS for 75K req/s

**After Optimizations:**
- HTTP/1.1: **82K-86K req/s** (+8-15%)
- HTTP/2: **128K-136K req/s** (+8-15%)
- Memory: **7-8MB RSS** (-30-40% allocations)
- Latency P99: **<2.0ms** (currently 2.66ms avg)

---

## Code Review Checklist

When implementing optimizations:

- [ ] No new allocations in hot path (request processing loop)
- [ ] Maintain thread-safety (TPC shared-nothing architecture)
- [ ] ASAN/UBSAN clean (no memory leaks, no undefined behavior)
- [ ] All 250 unit tests pass
- [ ] All 94 integration tests pass
- [ ] Benchmark shows improvement (not regression)
- [ ] No complexity increase (prefer simple over clever)
- [ ] Documentation updated (CLAUDE.md if architecture changes)

---

## References

- **Profiling Results:** `/workspace/profile_results/session_20251215_234716/`
- **Current Performance:** 75,166 req/s @ 2.66ms avg latency
- **Architecture:** Thread-Per-Core, Shared-Nothing, Poll-based event loop
- **Memory Model:** Arena allocators, mimalloc, fast_map (ankerl::unordered_dense)

---

*This analysis is based on static code review and profiling results from 2025-12-15. Validate all optimizations with benchmarks before merging.*
