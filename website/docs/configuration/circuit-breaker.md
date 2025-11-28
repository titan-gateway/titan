---
sidebar_position: 4
title: Circuit Breaker
description: Prevent cascading failures with automatic circuit breaking
---

# Circuit Breaker

Titan includes a built-in **circuit breaker** to prevent cascading failures when backends become unhealthy. When a backend starts failing, the circuit breaker automatically stops sending traffic to it, giving it time to recover.

## Overview

A circuit breaker acts like an electrical circuit breaker: when too many failures occur, it "opens" the circuit and rejects requests immediately instead of waiting for timeouts. This protects both your backend services and your users.

### State Machine

The circuit breaker operates in three states:

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

- **CLOSED** - Normal operation, all requests allowed
- **OPEN** - Too many failures detected, reject all requests (fast 503)
- **HALF_OPEN** - Testing recovery, allow limited requests

## Configuration

Add circuit breaker configuration to each upstream in your `config.json`:

```json
{
  "upstreams": [
    {
      "name": "backend",
      "backends": [
        { "host": "api.example.com", "port": 443 }
      ],
      "circuit_breaker": {
        "enabled": true,
        "failure_threshold": 5,
        "success_threshold": 2,
        "timeout_ms": 30000,
        "window_ms": 10000,
        "enable_global_hints": true,
        "catastrophic_threshold": 20
      }
    }
  ]
}
```

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enabled` | boolean | `true` | Enable/disable circuit breaker |
| `failure_threshold` | number | `5` | Number of failures in window to open circuit |
| `success_threshold` | number | `2` | Consecutive successes in HALF_OPEN to close circuit |
| `timeout_ms` | number | `30000` | Time in milliseconds before OPEN → HALF_OPEN (30s) |
| `window_ms` | number | `10000` | Sliding window in milliseconds for counting failures (10s) |
| `enable_global_hints` | boolean | `true` | Cross-worker catastrophic failure hints |
| `catastrophic_threshold` | number | `20` | Failures to trigger global hint flag |

## How It Works

### Failure Detection

The circuit breaker tracks **real request failures**:

- **5xx HTTP errors** (500, 502, 503, 504, etc.) count as failures
- **Connection timeouts** count as failures
- **Connection errors** count as failures
- **4xx errors** (400, 401, 403, 404, etc.) do NOT count as failures (client errors)
- **2xx and 3xx responses** count as successes

### Per-Worker Architecture

Titan uses a **thread-per-core** architecture with 4 workers. Each worker has its own independent circuit breaker for each backend:

- **Independent Decision Making** - If Worker 1's circuit opens, Workers 2-4 continue serving
- **Graceful Degradation** - System maintains partial capacity during failures
- **No Locking** - Zero synchronization overhead in hot path

### Global Catastrophic Failure Hints

When a worker detects a catastrophic failure rate (20+ failures), it sets a **global hint flag** to help other workers fail fast:

```cpp
// Worker 1 sees 20 failures in 5 seconds
→ Sets global flag: backend_down = true

// Workers 2, 3, 4 check global flag (cached read, zero contention)
→ Immediately reject requests without trying backend
→ Prevents wasting 60 more requests (20 failures × 3 workers)
```

This hybrid approach provides the best of both worlds:
- **99% of time:** Independent per-worker operation (zero synchronization)
- **Catastrophic failures:** Global coordination to minimize waste

## Integration with Health Checks

Circuit breaker works **alongside** health checks:

| Scenario | Health Check | Circuit Breaker |
|----------|--------------|-----------------|
| Backend dies | Detects in ~30s | Detects in ~2s (after 5 failures) |
| Intermittent 500s | Might miss | **Detects immediately** |
| Backend overloaded | Health OK, requests timeout | **Opens circuit, protects backend** |
| Low traffic period | **Still detects** (active probe) | Can't detect (no requests) |

**Both systems check** before sending request:
1. Health check status (background validation)
2. Circuit breaker state (real-time failure tracking)
3. Connection limit

## Example Scenarios

### Scenario 1: Backend Dies Suddenly

```
t=0s:   Backend process dies
t=0.5s: Worker 1 circuit opens (5 timeouts) ✅
t=1s:   Workers 2-4 circuits open
t=30s:  Health checker confirms: Unhealthy ✅

Result: Circuit breaker protected in 0.5s
        Health check confirmed in 30s
```

### Scenario 2: Backend Overloaded

```
t=0s:   Backend at 95% CPU, returning 503 for 20% of requests
t=1s:   Circuit breaker opens (5 failures in 10s window) ✅
t=30s:  Health check: /health endpoint still works → Healthy ✅

Result: Circuit breaker protects backend from cascading failure
        Health check doesn't mark it down (backend is alive, just overloaded)
        After 30s, circuit tries HALF_OPEN recovery test
```

### Scenario 3: Transient Network Glitch

```
t=0s:   100ms packet loss spike
t=0.1s: Worker 1 sees 3 failures
t=0.5s: Network recovers
t=1s:   Worker 1 circuit still CLOSED (only 3 failures, threshold is 5)

Result: Graceful handling - no circuit open for transient issues
```

## Monitoring

Titan exposes circuit breaker metrics via Prometheus endpoint (`/metrics`):

```prometheus
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

### Example Grafana Alerts

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

## Logs

Circuit breaker state transitions are logged:

```
[INFO] Circuit breaker CLOSED → OPEN for backend1 (5 failures in 10s)
[INFO] Circuit breaker OPEN → HALF_OPEN for backend1 (timeout expired, testing recovery)
[INFO] Circuit breaker HALF_OPEN → CLOSED for backend1 (2 consecutive successes)
[WARN] Circuit breaker HALF_OPEN → OPEN for backend1 (recovery test failed)
[WARN] Circuit breaker detected catastrophic failure rate (20 failures), setting global hint
```

## Best Practices

### 1. Tune Thresholds for Your Traffic

Default values work for most cases, but consider your traffic patterns:

- **High traffic** (1000+ req/s): Use higher `failure_threshold` (10-20) to avoid false positives
- **Low traffic** (<10 req/s): Use lower `failure_threshold` (3-5) for faster detection
- **Flaky network**: Increase `window_ms` to 30000ms (30s) to tolerate transient issues

### 2. Set Appropriate Timeout

- **Fast backends** (<100ms): Use shorter `timeout_ms` (10000ms = 10s)
- **Slow backends** (>1s): Use longer `timeout_ms` (60000ms = 60s)
- Balance between recovery speed and avoiding thundering herd

### 3. Monitor State Transitions

If circuit breaker is opening and closing frequently (>10 transitions/hour), investigate:
- Backend instability (fix root cause)
- Thresholds too aggressive (increase `failure_threshold` or `window_ms`)
- Timeouts too short (backend is slow but healthy)

### 4. Use with Retries Carefully

Circuit breakers and retries can interact poorly:

```json
{
  "max_retries": 2,          // DON'T: Retry immediately on failure
  "circuit_breaker": {
    "failure_threshold": 5    // Circuit opens after 5 failures
  }
}
```

With 2 retries, 2 original failures = 6 total attempts, opening circuit immediately.

**Recommendation:** Disable retries when circuit breaker is enabled, or use exponential backoff.

### 5. Test Circuit Breaker Behavior

Simulate backend failures to verify circuit breaker works:

```bash
# Kill backend to trigger circuit breaker
docker compose stop backend

# Send requests to Titan
wrk -t4 -c100 -d10s http://localhost:8080/api

# Check circuit breaker state
curl http://localhost:9090/metrics | grep circuit_breaker_state

# Restart backend and verify recovery
docker compose start backend
```

## Troubleshooting

### Circuit Stuck Open

**Problem:** Circuit breaker stays OPEN for extended periods

**Possible Causes:**
1. Backend is actually down (check health checks)
2. Recovery tests failing (backend not fully recovered)
3. Timeout too short for backend to recover

**Solution:**
- Check backend logs for errors
- Increase `timeout_ms` to give backend more recovery time
- Verify health check endpoint is working

### False Positives

**Problem:** Circuit opens during normal traffic spikes

**Possible Causes:**
1. `failure_threshold` too low for traffic volume
2. `window_ms` too short
3. Backend timeouts during legitimate load spikes

**Solution:**
- Increase `failure_threshold` (e.g., 10-20 for high traffic)
- Increase `window_ms` to 30000ms (30s)
- Add backend capacity or optimize slow endpoints

### No Circuit Opening

**Problem:** Backend is failing but circuit stays CLOSED

**Possible Causes:**
1. Failures are 4xx errors (client errors don't count)
2. Failures spread across multiple windows
3. Circuit breaker disabled

**Solution:**
- Verify backend is returning 5xx errors (not 4xx)
- Check `window_ms` is appropriate for failure rate
- Confirm `circuit_breaker.enabled = true` in config

## Further Reading

- [Architecture: Thread-Per-Core Design](/docs/architecture/overview)
- [Configuration: Health Checks](/docs/configuration/health-checks)
- [Deployment: Monitoring with Prometheus](/docs/deployment/monitoring)
