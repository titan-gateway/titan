# Titan Profiling - Critical Findings & High-Impact Optimizations

**Last Updated:** December 9, 2025
**Scenarios Profiled:** HTTP/1.1 Cleartext

---

## 🚨 Critical Findings

### 1. Excessive File Descriptor Close Operations

**Severity:** HIGH
**Impact:** 10-20% potential overhead reduction
**Data:** 1,196,320 `close_fd()` calls for 50,541 requests = 23.7 closes per request
**Expected:** 2-4 closes per request (client + backend)

**Analysis:**
- This is 6-12x higher than expected
- Indicates potential connection churn or pool inefficiency
- Each close is a syscall (~1-2μs overhead)
- Total waste: ~2.4 seconds of syscall time in 30s test

**Root Cause Hypotheses:**
1. Backend connections not being pooled properly
2. Connection pool health checks closing too aggressively
3. Error conditions forcing connection disposal
4. Duplicate close calls (defensive programming gone wrong)

**Investigation Steps:**
1. Add instrumentation to track every `close_fd()` call with stack trace
2. Count pool hits vs misses in `BackendConnectionPool::acquire()`
3. Check if `is_healthy()` is closing connections unnecessarily
4. Profile with single backend (eliminate DNS/connection variance)

**Files to Examine:**
- `src/core/server.cpp` - Connection lifecycle
- `src/gateway/connection_pool.cpp` - Pool acquire/release logic
- `src/core/socket.cpp` - close_fd implementation

---

### 2. Heavy Hashtable Operations (Header Maps)

**Severity:** MEDIUM
**Impact:** 5-15% potential reduction in allocations
**Data:** 43,303 hashtable assignments for 50,541 requests

**Analysis:**
- Each request triggers multiple header map copies
- `std::unordered_map` allocates per element
- 43,303 assignments = 0.86 per request (seems reasonable but...)
- Many are defensive copies that could be moves

**Root Cause:**
- `build_backend_request()` copies client headers to backend request
- Response headers may be copied multiple times through middleware
- No move semantics in header map passing

**Optimization Opportunity:**
```cpp
// Current (copy)
std::unordered_map<string, string> headers = request.headers;

// Optimized (move)
auto headers = std::move(request.headers); // if request not needed after

// Or use string_view map (zero-copy)
std::unordered_map<string_view, string_view> headers;
```

**Files to Examine:**
- `src/core/server.cpp:682-753` - `build_backend_request()`
- `src/gateway/pipeline.cpp` - Middleware header passing
- `src/http/http.cpp` - Response::add_header() implementation

---

### 3. String Allocation Overhead

**Severity:** MEDIUM
**Impact:** 5-10% reduction in malloc/free overhead
**Data:**
- 914,014 string deallocations (18 per request)
- 218,174 string constructions (4.3 per request)

**Analysis:**
- Ratio of 4:1 (deallocations:constructions) is actually good
- Indicates string_view is being used effectively
- But 18 deallocations per request is still significant
- Each malloc/free pair: ~50-100ns with mimalloc

**Root Cause:**
- Temporary strings in request processing
- Header value concatenation
- Path manipulation and query parsing
- Log message formatting

**Optimization Opportunity:**
- Per-request arena allocator (monotonic bump allocation)
- All temporary strings allocated from arena
- Single deallocation at request end

**Files to Examine:**
- `src/core/memory.hpp` - Arena allocator (already exists!)
- Request processing hot paths (need arena integration)
- `src/http/parser.cpp` - String construction during parsing

---

## 💡 High-Impact Optimization Opportunities

### Priority 1: Connection Pool Investigation (>10% gain)

**Objective:** Reduce `close_fd` calls from 23.7 to 2-4 per request

**Steps:**
1. **Instrumentation Phase:**
   ```cpp
   // Add to close_fd()
   static thread_local std::atomic<uint64_t> close_count{0};
   close_count++;
   if (close_count % 1000 == 0) {
       LOG_WARN("close_fd called {} times", close_count.load());
   }
   ```

2. **Pool Metrics:**
   ```cpp
   struct PoolMetrics {
       uint64_t acquire_hit = 0;
       uint64_t acquire_miss = 0;
       uint64_t health_check_fail = 0;
       uint64_t explicit_close = 0;
   };
   ```

3. **Expected Outcome:**
   - 80%+ pool hit rate (warm state)
   - <1 health check failure per 100 requests
   - close_fd calls drop to 4-6 per request

**Impact if Successful:** 10-20% overall throughput improvement

---

### Priority 2: Reduce Header Map Copies (5-15% gain)

**Objective:** Use move semantics and eliminate defensive copies

**Changes:**

**File: `src/core/server.cpp:682-753`**
```cpp
// Before
std::string build_backend_request(
    const Request& req,
    const std::unordered_map<string, string>& headers  // Copy!
) {
    // ...
}

// After
std::string build_backend_request(
    const Request& req,
    std::unordered_map<string, string>&& headers  // Move!
) {
    // ...
}

// Caller
auto backend_req = build_backend_request(
    request,
    std::move(request.headers)  // Move ownership
);
```

**Expected Outcome:**
- 43,303 → ~5,000 hashtable operations
- Eliminate most allocations in header processing
- Faster request forwarding

**Impact:** 5-15% reduction in request latency

---

### Priority 3: Arena Allocator Integration (5-10% gain)

**Objective:** Allocate all temporary request strings from arena

**Implementation:**

**File: `src/core/memory.hpp`** (already exists!)
```cpp
class Arena {
    // Already implemented, just need to use it!
};
```

**Changes Needed:**

1. **Add arena to RequestContext:**
```cpp
struct RequestContext {
    Arena request_arena{64 * 1024};  // 64KB per request
    // ... rest of fields
};
```

2. **Use arena for temporaries:**
```cpp
// String construction
auto* str = arena.allocate<char>(size);
// Use str, no need to free

// Header values
auto* header_val = arena.allocate<char>(value.size());
std::memcpy(header_val, value.data(), value.size());
```

3. **Reset on request completion:**
```cpp
ctx.request_arena.reset();  // Free all at once
```

**Expected Outcome:**
- 914,014 → ~100 deallocations (per-request reset only)
- Eliminate mimalloc overhead in hot path
- Better cache locality (sequential allocation)

**Impact:** 5-10% reduction in CPU time

---

### Priority 4: SIMD Header Parsing (5-10% gain)

**Objective:** Parallel scan for header name/value boundaries

**Current (Sequential):**
```cpp
// Find ':' separator
for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == ':') {
        name = line.substr(0, i);
        value = line.substr(i + 1);
        break;
    }
}
```

**Optimized (SIMD):**
```cpp
#include <arm_neon.h>  // ARM64 NEON

// Search 16 bytes at once
const char* find_colon_simd(const char* data, size_t len) {
    const uint8x16_t colon = vdupq_n_u8(':');

    for (size_t i = 0; i + 16 <= len; i += 16) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)(data + i));
        uint8x16_t cmp = vceqq_u8(chunk, colon);

        uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(
            vorr_u8(vget_low_u8(cmp), vget_high_u8(cmp))), 0);

        if (mask) {
            // Found colon, narrow down position
            // ...
        }
    }
}
```

**Expected Outcome:**
- 3-5x faster header separator search
- Reduce llhttp callback overhead
- Better for large header sets

**Impact:** 5-10% reduction in parsing time

---

### Priority 5: Middleware Fusion (2-5% gain)

**Objective:** Combine multiple middleware passes into single iteration

**Current:**
```cpp
// Phase 1: Request middleware (4 separate virtual calls)
for (auto& mw : request_middleware) {
    auto result = mw->process_request(ctx);  // Virtual dispatch
    if (result == Stop) return;
}

// Phase 2: Response middleware (3 separate virtual calls)
for (auto& mw : response_middleware) {
    mw->process_response(ctx);  // Virtual dispatch
}
```

**Optimized (Fused):**
```cpp
// Single pass with inlined logic
inline MiddlewareResult process_request_fused(RequestContext& ctx) {
    // Inline CORS check
    if (cors_enabled && ctx.request->method == OPTIONS) {
        // ...
        return Stop;
    }

    // Inline rate limit check
    if (!rate_limiter.allow(ctx.client_ip)) {
        // ...
        return Stop;
    }

    // Continue with proxy...
    return Continue;
}
```

**Expected Outcome:**
- Eliminate virtual function overhead (7 vtable lookups → 0)
- Better instruction cache locality
- Compiler can inline and optimize across boundaries

**Impact:** 2-5% reduction in middleware overhead

---

## 📊 Measured Performance (HTTP/1.1 Cleartext)

**Baseline:**
- **Throughput:** 1,679 req/s
- **Latency:** 215ms avg, 1.91s max (some timeouts)
- **Errors:** 0 (but 290 timeouts)

**Top Hotspots:**
1. llhttp parsing: 27% (well-optimized, minimal gains)
2. Backend events: 11% (efficient)
3. Strings: 8% (optimization target)
4. Headers: 5.4% (optimization target)
5. Middleware: 3% (acceptable)

**Memory:**
- String deallocations: 914,014 (18 per request)
- Hashtable operations: 43,303
- File descriptor closes: 1,196,320 ⚠️ **CRITICAL**

---

## 🎯 Expected Gains from All Optimizations

| Optimization | Impact | Confidence |
|--------------|--------|------------|
| Fix close_fd issue | 10-20% | High (if pool issue) |
| Reduce header copies | 5-15% | High |
| Arena allocator | 5-10% | Medium |
| SIMD header parsing | 5-10% | Medium |
| Middleware fusion | 2-5% | High |
| **Total Potential** | **27-60%** | - |

**Realistic Combined Gain:** 20-35% (optimizations don't stack linearly)

**Target Throughput:** 1,679 → 2,100-2,500 req/s

---

## 🔬 Next Profiling Scenarios

### Scenario 2: HTTP/2 with TLS
**Config:** `config/benchmark-http2-tls.json`
**Expected:** 40-60% TLS overhead (OpenSSL crypto)
**Key Metrics:** SSL_read/SSL_write time, handshake overhead

### Scenario 3: JWT Authentication
**Config:** `config/benchmark-jwt.json`
**Expected:** 30-50% JWT validation overhead
**Key Metrics:** EVP_DigestVerify time, key loading, cache hit rate

### Scenario 4: Connection Pool Stress
**Config:** `config/benchmark-pool.json`
**Expected:** DNS resolution blocking (10-50ms)
**Key Metrics:** getaddrinfo calls, pool hit rate, connection churn

### Scenario 5: Middleware Comparison
**Configs:** `benchmark-middleware-none.json` vs `benchmark-middleware-all.json`
**Expected:** Isolate middleware overhead
**Key Metrics:** Pipeline execution time, virtual dispatch overhead

---

## 📝 Investigation Checklist

- [ ] Instrument `close_fd()` with call counters and stack traces
- [ ] Add pool hit/miss metrics to `BackendConnectionPool`
- [ ] Profile with single backend (eliminate DNS variance)
- [ ] Measure header map copy frequency in `build_backend_request()`
- [ ] Test arena allocator integration with sample request
- [ ] Profile HTTP/2 with TLS scenario
- [ ] Profile JWT authentication scenario
- [ ] Profile connection pool stress scenario
- [ ] Profile middleware comparison
- [ ] Merge all results and create optimization roadmap

---

**Status:** HTTP/1.1 profiling complete. Moving to other scenarios...
