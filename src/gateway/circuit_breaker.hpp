/*
 * Copyright 2025 Titan Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Titan Circuit Breaker - Header
// Prevents cascading failures by tracking request failures and opening circuit

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string_view>

namespace titan::gateway {

/// Circuit breaker state
enum class CircuitState : uint8_t {
    CLOSED,     // Normal operation, requests allowed
    OPEN,       // Circuit protecting backend, requests rejected
    HALF_OPEN   // Testing recovery, limited requests allowed
};

/// Circuit breaker configuration
struct CircuitBreakerConfig {
    /// Number of failures in window to open circuit
    uint32_t failure_threshold = 5;

    /// Number of consecutive successes in HALF_OPEN to close circuit
    uint32_t success_threshold = 2;

    /// Time in milliseconds before OPEN → HALF_OPEN transition
    uint32_t timeout_ms = 30000;  // 30 seconds

    /// Sliding window in milliseconds for counting failures
    uint32_t window_ms = 10000;   // 10 seconds

    /// Enable catastrophic failure detection (sets global flag to help other workers)
    bool enable_global_hints = true;

    /// Number of failures to trigger global catastrophic failure hint
    uint32_t catastrophic_threshold = 20;
};

/// Circuit breaker for preventing cascading failures
///
/// State machine:
///   CLOSED → OPEN (failure_threshold failures in window_ms)
///   OPEN → HALF_OPEN (after timeout_ms)
///   HALF_OPEN → CLOSED (success_threshold consecutive successes)
///   HALF_OPEN → OPEN (any failure)
///
/// Thread-safety: Designed for single-worker access (thread-per-core).
/// Metrics are atomic for cross-thread observability.
class CircuitBreaker {
public:
    explicit CircuitBreaker(CircuitBreakerConfig config);
    ~CircuitBreaker() = default;

    // Non-copyable, movable
    CircuitBreaker(const CircuitBreaker&) = delete;
    CircuitBreaker& operator=(const CircuitBreaker&) = delete;
    CircuitBreaker(CircuitBreaker&&) noexcept;
    CircuitBreaker& operator=(CircuitBreaker&&) noexcept;

    /// Check if request should be allowed through circuit
    /// Returns false if circuit is OPEN, true if CLOSED or HALF_OPEN
    [[nodiscard]] bool should_allow_request();

    /// Record successful request completion
    void record_success();

    /// Record failed request (5xx error or timeout)
    void record_failure();

    /// Force circuit to OPEN state (used by health checker)
    void force_open();

    /// Get current circuit state
    [[nodiscard]] CircuitState get_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    /// Get total failures recorded
    [[nodiscard]] uint64_t get_total_failures() const noexcept {
        return total_failures_.load(std::memory_order_relaxed);
    }

    /// Get total successes recorded
    [[nodiscard]] uint64_t get_total_successes() const noexcept {
        return total_successes_.load(std::memory_order_relaxed);
    }

    /// Get total requests rejected by circuit
    [[nodiscard]] uint64_t get_rejected_requests() const noexcept {
        return rejected_requests_.load(std::memory_order_relaxed);
    }

    /// Get total state transitions
    [[nodiscard]] uint64_t get_state_transitions() const noexcept {
        return state_transitions_.load(std::memory_order_relaxed);
    }

    /// Get configuration
    [[nodiscard]] const CircuitBreakerConfig& config() const noexcept {
        return config_;
    }

private:
    /// Transition to new state and update metrics
    void transition_to(CircuitState new_state);

    /// Remove failures older than window_ms
    void cleanup_old_failures();

    /// Try transitioning from OPEN to HALF_OPEN if timeout expired
    /// Returns true if request should be allowed
    bool try_half_open();

    /// Check if global catastrophic failure flag is set
    bool is_global_catastrophic_failure() const;

    /// Set global catastrophic failure flag (helps other workers fail fast)
    void set_global_catastrophic_failure();

    /// Clear global catastrophic failure flag (when circuit recovers)
    void clear_global_catastrophic_failure();

    // Configuration
    CircuitBreakerConfig config_;

    // State (atomic for observability from other threads)
    std::atomic<CircuitState> state_{CircuitState::CLOSED};

    // Sliding window of failure timestamps (not thread-safe, single-worker access)
    std::deque<std::chrono::steady_clock::time_point> failure_timestamps_;

    // HALF_OPEN state tracking
    uint32_t consecutive_successes_ = 0;

    // Time when state last changed (for OPEN → HALF_OPEN timeout)
    std::chrono::steady_clock::time_point state_transition_time_;

    // Metrics (atomic for cross-thread observability)
    std::atomic<uint64_t> total_failures_{0};
    std::atomic<uint64_t> total_successes_{0};
    std::atomic<uint64_t> rejected_requests_{0};
    std::atomic<uint64_t> state_transitions_{0};

    // Global catastrophic failure flags (shared across workers)
    // Index by backend_id (for now, use simple array, later integrate with Upstream)
    static constexpr size_t MAX_BACKENDS = 256;
    static std::atomic<bool> global_backend_down_[MAX_BACKENDS];
    size_t backend_id_ = 0;  // Set during construction, for now defaults to 0
};

/// Convert circuit state to string for logging
[[nodiscard]] constexpr std::string_view to_string(CircuitState state) noexcept {
    switch (state) {
        case CircuitState::CLOSED:
            return "CLOSED";
        case CircuitState::OPEN:
            return "OPEN";
        case CircuitState::HALF_OPEN:
            return "HALF_OPEN";
    }
    return "UNKNOWN";
}

} // namespace titan::gateway
