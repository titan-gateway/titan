# Detecting Failures in Titan: Circuit Breaker + Health Check Integration

**Last Updated:** 2025-11-28
**Status:** Design Document for Phase 13.1 Implementation

---

## Executive Summary

Titan uses **two complementary systems** for detecting and handling backend failures:

1. **Circuit Breaker** - Real-time, request-level failure tracking (sub-second detection)
2. **Health Checker** - Proactive background validation (periodic active probes)

These systems work **together**, not as replacements. Circuit breakers provide fast fail-fast protection during traffic surges and cascading failures, while health checkers validate backend availability even during low-traffic periods.

---

## Table of Contents

1. [Architecture Decision: Per-Worker vs Global Circuit Breakers](#architecture-decision)
2. [System Comparison: Circuit Breaker vs Health Checker](#system-comparison)
3. [Integration Strategy](#integration-strategy)
4. [Implementation Details](#implementation-details)
5. [Failure Scenarios](#failure-scenarios)
6. [Monitoring and Metrics](#monitoring-and-metrics)

---

## Architecture Decision: Per-Worker vs Global Circuit Breakers {#architecture-decision}

### The Question

With Titan's thread-per-core architecture (4 workers), should each worker have an **independent circuit breaker**, or should all workers share a **global circuit breaker**?

### Option A: Per-Worker Independent Circuit Breakers (CHOSEN)

Each worker thread maintains its own circuit breaker instance. If 2 out of 4 workers open their circuits, the other 2 continue attempting requests.

**PROS:**

1. **Zero Synchronization Overhead** - Aligns perfectly with Titan's shared-nothing TPC architecture. No atomics, no message passing, no cache line bouncing.

2. **Graceful Degradation** - System maintains partial capacity during failures. If backend has intermittent issues, some workers might succeed while others protect themselves.

3. **False Positive Isolation** - If one worker's network path to backend is broken but others are fine, only that worker opens circuit. Prevents unnecessary global outage.

4. **Better for Transient Errors** - Short network glitches (100ms packet loss) won't cascade to full traffic block.

5. **Exploits Load Balancer Diversity** - Different workers might connect to different backend instances in a replicated service.

**CONS:**

1. **Delayed Global Failure Detection** - If backend is truly dead, each worker must hit failure threshold independently. With `failure_threshold = 5`, you'll send 20 failing requests (5 per worker) before all workers protect the backend.

2. **Wasted Backend Resources** - While 2 workers have circuit open, other 2 are still sending traffic to failing backend. Could delay backend recovery.

3. **Inconsistent User Experience** - Users hitting Worker 1 (circuit open) get instant 503, users hitting Worker 2 (circuit closed) wait for timeout then get 502.

4. **Metric Aggregation Complexity** - Prometheus metrics need per-worker labels. Dashboard shows "Circuit State: 2 OPEN, 2 CLOSED" which is harder to alert on.

5. **Risk of Thundering Herd** - When circuits transition HALF_OPEN → CLOSED at different times, you get uncoordinated recovery waves.

### Option B: Global Shared Circuit Breaker

All workers share a single circuit breaker instance with atomic state.

**PROS:**

1. **Fast Fail-Fast** - One worker detects failure, all workers immediately stop sending traffic. Minimizes wasted requests.

2. **Consistent User Experience** - All users get same behavior (either success or fast 503).

3. **Better Backend Protection** - Failing backend gets immediate relief from ALL workers, maximizing recovery time.

4. **Simpler Monitoring** - Single circuit state: "Backend X circuit is OPEN". Easy to alert on.

5. **Coordinated Recovery** - All workers test backend simultaneously in HALF_OPEN, faster to detect recovery.

**CONS:**

1. **Synchronization Overhead** - Requires `std::atomic` shared across cores or message passing. Violates shared-nothing principle.

2. **Cache Line Contention** - 4 workers reading/writing same atomic = constant cache invalidation. Could add 50-100ns per request.

3. **False Positive Amplification** - One worker's transient network hiccup opens circuit for all workers, even if backend is healthy for others.

4. **Complexity** - Need shared memory region or cross-worker channels. Breaks clean TPC model.

### Hybrid Approach (RECOMMENDED)

Use **per-worker circuit breakers** (independent) but with **cross-worker hints** for catastrophic failures.

```cpp
// Per-worker circuit breaker (default behavior)
class CircuitBreaker {
    std::atomic<CircuitState> state_{CircuitState::CLOSED};

    // Local failure tracking (no cross-worker sharing)
    std::deque<time_point> failure_timestamps_;

    // Optional: Global catastrophic failure flag (rare writes, many reads)
    static std::atomic<bool> global_backend_down_[MAX_BACKENDS];

    bool should_allow_request() {
        // Fast path: Check global flag (read-only, cached)
        if (global_backend_down_[backend_id_].load(std::memory_order_relaxed)) {
            return false;  // Backend marked as catastrophically down
        }

        // Normal path: Check local circuit state
        auto state = state_.load(std::memory_order_acquire);
        if (state == CircuitState::OPEN) {
            return try_half_open();
        }
        return true;
    }

    void record_failure() {
        failure_timestamps_.push_back(now());
        cleanup_old_failures();

        // If we've seen catastrophic failure rate (e.g., 20 failures in 5s)
        if (failure_timestamps_.size() > 20) {
            // Rare write: Set global flag to help other workers fail fast
            global_backend_down_[backend_id_].store(true, std::memory_order_release);
        }

        if (failure_timestamps_.size() >= config_.failure_threshold) {
            transition_to(CircuitState::OPEN);
        }
    }
};
```

**How This Works:**

- **99% of time:** Each worker operates independently (per-worker circuit breaker)
- **Catastrophic failure:** If a worker sees extreme failure rate (20+ failures), it sets a global hint flag
- **Other workers:** Read global flag (cached, read-only, zero contention) and fail fast
- **Recovery:** Global flag cleared when any worker's circuit returns to CLOSED

**Benefits:**

- Maintains shared-nothing architecture for normal operations
- Fast global fail-fast for truly dead backends
- Minimal synchronization (global flag written once per catastrophic event, not per request)
- Graceful degradation for transient/partial failures

---

## System Comparison: Circuit Breaker vs Health Checker {#system-comparison}

Titan has **two orthogonal systems** for tracking backend health. They serve different purposes and should work **together**.

### System 1: HealthChecker (Proactive Background Validation)

**Location:** `src/control/health.hpp`, `src/control/health.cpp`

**Purpose:** Periodic active health checks

- Runs every 30 seconds (configurable)
- Sends HTTP GET to `/health` endpoint
- Updates `Backend::status` enum (Healthy/Degraded/Unhealthy)
- Tracks `consecutive_failures` counter

**Characteristics:**

- **Slow to detect failures:** Up to 30s lag between backend death and detection
- **No user impact:** Health checks don't block user requests
- **Active probing:** Can check `/health` endpoint even if no user traffic
- **Backend cooperation required:** Needs backend to expose `/health` endpoint

### System 2: Circuit Breaker (Reactive Real-Time Tracking)

**Location:** `src/gateway/circuit_breaker.hpp` (to be implemented)

**Purpose:** Request-level failure tracking

- Monitors actual user request results
- Fast detection (5 failures in 10s window)
- Prevents cascading failures
- Works without backend cooperation

**Characteristics:**

- **Fast to detect failures:** Milliseconds to seconds
- **User-impacting:** Based on real request failures
- **Passive monitoring:** Only knows what user traffic reveals
- **No traffic = no detection:** Can't detect failures if no requests

### Comparison Matrix

| Scenario | HealthChecker | Circuit Breaker |
|----------|---------------|-----------------|
| Backend process dies | Detects in ~30s | Detects in ~2s (after 5 failures) |
| Intermittent 500 errors | Might miss (if /health still works) | **Detects immediately** |
| Slow responses (timeouts) | Might miss (if /health responds fast) | **Detects immediately** |
| Backend under load | Health check succeeds, but user requests timeout | **Circuit opens, protects backend** |
| Network partition | Detects via TCP timeout | Detects via request failures |
| Low traffic period | **Still detects** (active probe) | Can't detect (no requests to monitor) |
| Backend /health endpoint broken | **Shows as unhealthy** | User traffic might still work (false positive) |

**Conclusion:** They solve different problems. Use **BOTH** in series.

---

## Integration Strategy {#integration-strategy}

### Decision Flow

```
Request Arrives
    ↓
Check HealthChecker Status (Backend::is_available())
    ↓ (Unhealthy/Draining)
    ├─→ Return 503 (Backend marked down by health checks)
    ↓ (Healthy/Degraded)
Check Circuit Breaker State (circuit_breaker->should_allow_request())
    ↓ (OPEN - circuit protecting backend)
    ├─→ Return 503 (Too many recent failures)
    ↓ (CLOSED/HALF_OPEN - circuit allows request)
Check Connection Limit (active_connections < max_connections)
    ↓ (>= max)
    ├─→ Return 503 (Connection limit reached)
    ↓ (< max)
Send Request to Backend ✅
```

### Updated Backend::can_accept_connection()

```cpp
struct Backend {
    // Existing fields...
    std::unique_ptr<CircuitBreaker> circuit_breaker;  // NEW

    [[nodiscard]] bool can_accept_connection() const noexcept {
        // Gate 1: Health check status (background validation)
        if (!is_available()) {
            return false;  // HealthChecker marked backend as down
        }

        // Gate 2: Circuit breaker state (real-time failure tracking)
        if (circuit_breaker && !circuit_breaker->should_allow_request()) {
            return false;  // Too many recent failures, circuit is OPEN
        }

        // Gate 3: Connection limit (existing logic)
        if (active_connections >= max_connections) {
            return false;
        }

        return true;
    }
};
```

### Recording Failures and Successes

Circuit breakers need feedback from actual requests. Update `ProxyMiddleware`:

```cpp
// After backend request completes:
void ProxyMiddleware::on_backend_response(Backend* backend, Response& response) {
    if (response.status_code >= 500) {
        // Record server error in circuit breaker
        if (backend->circuit_breaker) {
            backend->circuit_breaker->record_failure();
        }

        // Also update health check counters
        backend->consecutive_failures++;
        backend->total_failures++;
    } else if (response.status_code < 400) {
        // Record success
        if (backend->circuit_breaker) {
            backend->circuit_breaker->record_success();
        }
        backend->consecutive_failures = 0;
    }
}

void ProxyMiddleware::on_backend_timeout(Backend* backend) {
    // Timeout is a circuit breaker failure
    if (backend->circuit_breaker) {
        backend->circuit_breaker->record_failure();
    }
    backend->consecutive_failures++;
    backend->total_failures++;
}
```

### HealthChecker Integration

Health checker can force circuit breaker to stay open when backend is marked unhealthy:

```cpp
void HealthChecker::run_health_check(Backend& backend) {
    auto health = check_backend(backend.host, backend.port);

    if (health.status == HealthStatus::Unhealthy) {
        backend.status = BackendStatus::Unhealthy;
        backend.consecutive_failures++;

        // Keep circuit OPEN until health check confirms recovery
        if (backend.circuit_breaker) {
            backend.circuit_breaker->force_open();
        }
    } else if (health.status == HealthStatus::Healthy) {
        backend.status = BackendStatus::Healthy;
        backend.consecutive_failures = 0;
        // Circuit breaker will recover naturally through HALF_OPEN state
    }
}
```

---

## Implementation Details {#implementation-details}

### Circuit Breaker State Machine

```
         record_failure() ×5 in 10s
CLOSED ──────────────────────────────→ OPEN
  ↑                                      │
  │                                      │ timeout (30s)
  │                                      ↓
  │                                  HALF_OPEN
  │                                      │
  │     record_success() ×2              │ record_failure()
  └──────────────────────────            │
                                          ↓
                                        OPEN
```

**States:**

- **CLOSED:** Normal operation, all requests allowed
- **OPEN:** Circuit protecting backend, all requests rejected (fast 503)
- **HALF_OPEN:** Testing recovery, limited requests allowed (success threshold = 2)

### Configuration Schema

```json
{
  "upstreams": [{
    "name": "backend",
    "circuit_breaker": {
      "enabled": true,
      "failure_threshold": 5,
      "success_threshold": 2,
      "timeout_ms": 30000,
      "window_ms": 10000
    },
    "backends": [...]
  }]
}
```

### CircuitBreaker Class Interface

```cpp
enum class CircuitState : uint8_t {
  CLOSED,     // Normal operation
  OPEN,       // Rejecting requests
  HALF_OPEN   // Testing recovery
};

struct CircuitBreakerConfig {
  uint32_t failure_threshold = 5;    // Failures to open circuit
  uint32_t success_threshold = 2;     // Successes to close circuit
  uint32_t timeout_ms = 30000;        // Time before OPEN → HALF_OPEN
  uint32_t window_ms = 10000;         // Sliding window for failures
};

class CircuitBreaker {
public:
  explicit CircuitBreaker(CircuitBreakerConfig config);

  // Check if request should be allowed
  [[nodiscard]] bool should_allow_request();

  // Record request outcome
  void record_success();
  void record_failure();

  // Force circuit to stay open (used by health checker)
  void force_open();

  // Metrics
  [[nodiscard]] CircuitState get_state() const;
  [[nodiscard]] uint64_t get_total_failures() const;
  [[nodiscard]] uint64_t get_rejected_requests() const;

private:
  void transition_to(CircuitState new_state);
  void cleanup_old_failures();
  bool try_half_open();

  CircuitBreakerConfig config_;
  std::atomic<CircuitState> state_{CircuitState::CLOSED};

  // Sliding window of failure timestamps
  std::deque<std::chrono::steady_clock::time_point> failure_timestamps_;

  // HALF_OPEN state tracking
  uint32_t consecutive_successes_ = 0;
  std::chrono::steady_clock::time_point state_transition_time_;

  // Metrics (atomic for thread-safe reads)
  std::atomic<uint64_t> total_failures_{0};
  std::atomic<uint64_t> total_successes_{0};
  std::atomic<uint64_t> rejected_requests_{0};
  std::atomic<uint64_t> state_transitions_{0};
};
```

---

## Failure Scenarios {#failure-scenarios}

### Scenario 1: Backend Dies Suddenly

```
t=0s:   Backend process dies
t=0.1s: Worker 1 sends request → timeout → circuit records failure (1/5)
t=0.2s: Worker 1 sends request → timeout → circuit records failure (2/5)
t=0.5s: Worker 1 sends request → timeout → circuit records failure (5/5)
t=0.5s: Worker 1 circuit breaker → OPEN state ✅ (stops sending traffic)
t=1s:   Workers 2, 3, 4 also experience failures, circuits open
t=30s:  HealthChecker runs next check → marks backend Unhealthy ✅

Result: Circuit breaker protected backend in 0.5s, health check confirmed in 30s
```

### Scenario 2: Backend Overloaded (Intermittent 503s)

```
t=0s:   Backend at 95% CPU, starts returning 503 for 20% of requests
t=1s:   Circuit breaker sees 5 failures in 10s window → OPEN ✅
t=30s:  HealthChecker runs → /health endpoint still works (200 OK) → Healthy ✅

Result: Circuit breaker protects backend from cascading failure,
        health check doesn't mark it as down (backend is alive, just overloaded).
        After 30s circuit tries HALF_OPEN, if backend recovered, circuit closes.
```

### Scenario 3: Network Partition

```
t=0s:   Network path from Worker 1 to backend fails (firewall rule)
t=1s:   Worker 1 circuit → OPEN (network timeouts)
t=30s:  Worker 1 HealthChecker → Unhealthy (can't reach backend)
        Workers 2, 3, 4 → Healthy (their network paths work)

Result: Worker 1 stops sending traffic (circuit + health check),
        Workers 2-4 continue serving (independent per-worker state)
```

### Scenario 4: Transient Network Glitch

```
t=0s:   100ms packet loss spike
t=0.1s: Worker 1 sees 3 failures in 100ms
t=0.5s: Network recovers
t=1s:   Worker 1 circuit still CLOSED (only 3 failures, threshold is 5)

Result: Graceful handling - no circuit open for transient issues
```

### Scenario 5: Catastrophic Backend Failure (All Workers)

```
t=0s:   Backend returns 500 for ALL requests
t=0.5s: Worker 1 sees 20 failures → sets global_backend_down flag
t=0.5s: Workers 2, 3, 4 read flag → immediately fail fast
t=1s:   All 4 workers have circuit OPEN ✅
t=30s:  All 4 workers try HALF_OPEN (testing recovery)

Result: Hybrid approach - Worker 1 opens circuit independently,
        sets global flag, other workers fail fast without wasting 15 more requests
```

---

## Monitoring and Metrics {#monitoring-and-metrics}

### Prometheus Metrics

```
# Circuit breaker state per backend (0=CLOSED, 1=OPEN, 2=HALF_OPEN)
titan_circuit_breaker_state{backend="backend1",worker="0"} 0

# Total failures recorded
titan_circuit_breaker_failures_total{backend="backend1",worker="0"} 42

# Total successes recorded
titan_circuit_breaker_successes_total{backend="backend1",worker="0"} 1523

# Requests rejected by circuit breaker
titan_circuit_breaker_rejected_total{backend="backend1",worker="0"} 187

# State transitions (CLOSED→OPEN, OPEN→HALF_OPEN, etc.)
titan_circuit_breaker_transitions_total{backend="backend1",worker="0"} 3
```

### Alerting Rules

```yaml
# Alert when any worker's circuit is OPEN for >5 minutes
- alert: CircuitBreakerOpenTooLong
  expr: titan_circuit_breaker_state > 0 and changes(titan_circuit_breaker_state[5m]) == 0
  for: 5m
  annotations:
    summary: "Circuit breaker for {{ $labels.backend }} stuck OPEN on worker {{ $labels.worker }}"

# Alert when majority of workers have circuit OPEN
- alert: CircuitBreakerMajorityOpen
  expr: count(titan_circuit_breaker_state{state="1"} == 1) by (backend) >= 3
  annotations:
    summary: "Majority of workers have circuit OPEN for {{ $labels.backend }}"
```

### Logging

```
[INFO] Circuit breaker CLOSED → OPEN for backend1 (5 failures in 10s)
[INFO] Circuit breaker OPEN → HALF_OPEN for backend1 (timeout expired, testing recovery)
[INFO] Circuit breaker HALF_OPEN → CLOSED for backend1 (2 consecutive successes)
[WARN] Circuit breaker HALF_OPEN → OPEN for backend1 (recovery test failed)
```

---

## Decision Matrix: When to Send Request?

| Health Check | Circuit Breaker | Connections | Decision |
|--------------|-----------------|-------------|----------|
| Healthy      | CLOSED          | < max       | ✅ Send   |
| Healthy      | CLOSED          | >= max      | ❌ Limit  |
| Healthy      | OPEN            | any         | ❌ Circuit|
| Healthy      | HALF_OPEN       | any         | ✅ Test   |
| Degraded     | CLOSED          | < max       | ✅ Send   |
| Degraded     | OPEN            | any         | ❌ Circuit|
| Unhealthy    | any             | any         | ❌ Health |
| Draining     | any             | any         | ❌ Drain  |

---

## References

- [Martin Fowler: Circuit Breaker Pattern](https://martinfowler.com/bliki/CircuitBreaker.html)
- [Release It! - Michael Nygard (Circuit Breaker Pattern)](https://pragprog.com/titles/mnee2/release-it-second-edition/)
- [Hystrix Design Principles](https://github.com/Netflix/Hystrix/wiki/How-it-Works)
- [Envoy Circuit Breaking](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/upstream/circuit_breaking)

---

**End of Document**
