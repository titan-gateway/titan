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

// Titan Circuit Breaker - Implementation

#include "circuit_breaker.hpp"

#include <fmt/core.h>

#include <algorithm>

namespace titan::gateway {

// Initialize global catastrophic failure flags
std::atomic<bool> CircuitBreaker::global_backend_down_[CircuitBreaker::MAX_BACKENDS];

CircuitBreaker::CircuitBreaker(CircuitBreakerConfig config)
    : config_(config)
    , state_transition_time_(std::chrono::steady_clock::now()) {
    // Initialize global flags to false (could be done at static init, but explicit is better)
    for (size_t i = 0; i < MAX_BACKENDS; ++i) {
        global_backend_down_[i].store(false, std::memory_order_relaxed);
    }
}

CircuitBreaker::CircuitBreaker(CircuitBreaker&& other) noexcept
    : config_(other.config_)
    , state_(other.state_.load(std::memory_order_acquire))
    , failure_timestamps_(std::move(other.failure_timestamps_))
    , consecutive_successes_(other.consecutive_successes_)
    , state_transition_time_(other.state_transition_time_)
    , total_failures_(other.total_failures_.load(std::memory_order_relaxed))
    , total_successes_(other.total_successes_.load(std::memory_order_relaxed))
    , rejected_requests_(other.rejected_requests_.load(std::memory_order_relaxed))
    , state_transitions_(other.state_transitions_.load(std::memory_order_relaxed))
    , backend_id_(other.backend_id_) {}

CircuitBreaker& CircuitBreaker::operator=(CircuitBreaker&& other) noexcept {
    if (this != &other) {
        config_ = other.config_;
        state_.store(other.state_.load(std::memory_order_acquire), std::memory_order_release);
        failure_timestamps_ = std::move(other.failure_timestamps_);
        consecutive_successes_ = other.consecutive_successes_;
        state_transition_time_ = other.state_transition_time_;
        total_failures_.store(other.total_failures_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        total_successes_.store(other.total_successes_.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        rejected_requests_.store(other.rejected_requests_.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        state_transitions_.store(other.state_transitions_.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        backend_id_ = other.backend_id_;
    }
    return *this;
}

bool CircuitBreaker::should_allow_request() {
    // Fast path: Check global catastrophic failure flag (read-only, cached)
    if (config_.enable_global_hints && is_global_catastrophic_failure()) {
        rejected_requests_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto state = state_.load(std::memory_order_acquire);

    switch (state) {
        case CircuitState::CLOSED:
            // Normal operation, allow all requests
            return true;

        case CircuitState::OPEN:
            // Circuit is open, check if timeout expired
            return try_half_open();

        case CircuitState::HALF_OPEN:
            // In recovery testing mode, allow requests but watch for failures
            return true;
    }

    return false; // Unreachable, but satisfies compiler
}

void CircuitBreaker::record_success() {
    total_successes_.fetch_add(1, std::memory_order_relaxed);

    auto state = state_.load(std::memory_order_acquire);

    if (state == CircuitState::HALF_OPEN) {
        // In HALF_OPEN, count consecutive successes
        consecutive_successes_++;

        if (consecutive_successes_ >= config_.success_threshold) {
            // Recovery successful, close circuit
            transition_to(CircuitState::CLOSED);
            consecutive_successes_ = 0;

            // Clear global catastrophic flag if we set it
            if (config_.enable_global_hints) {
                clear_global_catastrophic_failure();
            }

            // Clear failure history
            failure_timestamps_.clear();

            fmt::print("[INFO] Circuit breaker HALF_OPEN → CLOSED (recovery successful)\n");
        }
    }
}

void CircuitBreaker::record_failure() {
    total_failures_.fetch_add(1, std::memory_order_relaxed);

    auto now = std::chrono::steady_clock::now();
    failure_timestamps_.push_back(now);

    // Cleanup old failures outside sliding window
    cleanup_old_failures();

    auto state = state_.load(std::memory_order_acquire);

    if (state == CircuitState::HALF_OPEN) {
        // Recovery test failed, reopen circuit
        transition_to(CircuitState::OPEN);
        consecutive_successes_ = 0;
        fmt::print("[WARN] Circuit breaker HALF_OPEN → OPEN (recovery test failed)\n");
        return;
    }

    if (state == CircuitState::CLOSED) {
        // Check if we've hit failure threshold
        if (failure_timestamps_.size() >= config_.failure_threshold) {
            transition_to(CircuitState::OPEN);
            fmt::print(
                "[INFO] Circuit breaker CLOSED → OPEN ({} failures in {}ms window)\n",
                failure_timestamps_.size(),
                config_.window_ms);
        }

        // Check for catastrophic failure rate (help other workers)
        if (config_.enable_global_hints &&
            failure_timestamps_.size() >= config_.catastrophic_threshold) {
            set_global_catastrophic_failure();
            fmt::print(
                "[WARN] Circuit breaker detected catastrophic failure rate ({} failures), "
                "setting global hint\n",
                failure_timestamps_.size());
        }
    }
}

void CircuitBreaker::force_open() {
    auto current_state = state_.load(std::memory_order_acquire);
    if (current_state != CircuitState::OPEN) {
        transition_to(CircuitState::OPEN);
        fmt::print("[INFO] Circuit breaker forced to OPEN state by health checker\n");
    }
}

void CircuitBreaker::transition_to(CircuitState new_state) {
    auto old_state = state_.exchange(new_state, std::memory_order_acq_rel);

    if (old_state != new_state) {
        state_transition_time_ = std::chrono::steady_clock::now();
        state_transitions_.fetch_add(1, std::memory_order_relaxed);
    }
}

void CircuitBreaker::cleanup_old_failures() {
    if (failure_timestamps_.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto window = std::chrono::milliseconds(config_.window_ms);
    auto cutoff = now - window;

    // Remove failures older than window
    while (!failure_timestamps_.empty() && failure_timestamps_.front() < cutoff) {
        failure_timestamps_.pop_front();
    }
}

bool CircuitBreaker::try_half_open() {
    // Check if timeout has expired since transition to OPEN
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(config_.timeout_ms);
    auto time_in_open = now - state_transition_time_;

    if (time_in_open >= timeout) {
        // Timeout expired, try transitioning to HALF_OPEN
        auto expected = CircuitState::OPEN;
        if (state_.compare_exchange_strong(
                expected, CircuitState::HALF_OPEN, std::memory_order_acq_rel)) {
            state_transition_time_ = now;
            state_transitions_.fetch_add(1, std::memory_order_relaxed);
            consecutive_successes_ = 0;
            fmt::print(
                "[INFO] Circuit breaker OPEN → HALF_OPEN (timeout expired, testing recovery)\n");
            return true; // Allow this request through as recovery test
        }
    }

    // Still in OPEN state and timeout not expired
    rejected_requests_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool CircuitBreaker::is_global_catastrophic_failure() const {
    return global_backend_down_[backend_id_].load(std::memory_order_relaxed);
}

void CircuitBreaker::set_global_catastrophic_failure() {
    global_backend_down_[backend_id_].store(true, std::memory_order_release);
}

void CircuitBreaker::clear_global_catastrophic_failure() {
    global_backend_down_[backend_id_].store(false, std::memory_order_release);
}

} // namespace titan::gateway
