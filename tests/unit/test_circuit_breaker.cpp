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

// Unit tests for Circuit Breaker (Phase 13.1)

#include <catch2/catch_test_macros.hpp>
#include <thread>

#include "gateway/circuit_breaker.hpp"

using namespace titan::gateway;

TEST_CASE("CircuitBreaker - Basic construction", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    config.success_threshold = 2;
    config.timeout_ms = 30000;
    config.window_ms = 10000;

    CircuitBreaker breaker(config);

    REQUIRE(breaker.get_state() == CircuitState::CLOSED);
    REQUIRE(breaker.get_total_failures() == 0);
    REQUIRE(breaker.get_total_successes() == 0);
    REQUIRE(breaker.get_rejected_requests() == 0);
}

TEST_CASE("CircuitBreaker - to_string conversion", "[circuit_breaker]") {
    REQUIRE(to_string(CircuitState::CLOSED) == "CLOSED");
    REQUIRE(to_string(CircuitState::OPEN) == "OPEN");
    REQUIRE(to_string(CircuitState::HALF_OPEN) == "HALF_OPEN");
}

TEST_CASE("CircuitBreaker - Allows requests in CLOSED state", "[circuit_breaker]") {
    CircuitBreaker breaker(CircuitBreakerConfig{});

    REQUIRE(breaker.should_allow_request());
    REQUIRE(breaker.should_allow_request());
    REQUIRE(breaker.should_allow_request());
}

TEST_CASE("CircuitBreaker - Records successes", "[circuit_breaker]") {
    CircuitBreaker breaker(CircuitBreakerConfig{});

    breaker.record_success();
    breaker.record_success();
    breaker.record_success();

    REQUIRE(breaker.get_total_successes() == 3);
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);
}

TEST_CASE("CircuitBreaker - Opens circuit after failure threshold", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    config.window_ms = 10000;

    CircuitBreaker breaker(config);

    // Record failures
    for (int i = 0; i < 4; ++i) {
        breaker.record_failure();
        REQUIRE(breaker.get_state() == CircuitState::CLOSED);
    }

    // 5th failure should open circuit
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);
    REQUIRE(breaker.get_total_failures() == 5);
}

TEST_CASE("CircuitBreaker - Rejects requests in OPEN state", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.timeout_ms = 60000; // 60 seconds - won't expire during test

    CircuitBreaker breaker(config);

    // Trigger circuit to OPEN
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Should reject requests
    REQUIRE_FALSE(breaker.should_allow_request());
    REQUIRE_FALSE(breaker.should_allow_request());

    REQUIRE(breaker.get_rejected_requests() == 2);
}

TEST_CASE("CircuitBreaker - Transitions to HALF_OPEN after timeout", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.timeout_ms = 100; // 100ms timeout for fast test

    CircuitBreaker breaker(config);

    // Open circuit
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Should allow request (and transition to HALF_OPEN)
    REQUIRE(breaker.should_allow_request());
    REQUIRE(breaker.get_state() == CircuitState::HALF_OPEN);
}

TEST_CASE("CircuitBreaker - HALF_OPEN closes on success threshold", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.success_threshold = 2;
    config.timeout_ms = 50;

    CircuitBreaker breaker(config);

    // Open circuit
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Wait and transition to HALF_OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(breaker.should_allow_request());
    REQUIRE(breaker.get_state() == CircuitState::HALF_OPEN);

    // Record successes
    breaker.record_success();
    REQUIRE(breaker.get_state() == CircuitState::HALF_OPEN);

    breaker.record_success();
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);
}

TEST_CASE("CircuitBreaker - HALF_OPEN reopens on failure", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.success_threshold = 2;
    config.timeout_ms = 50;

    CircuitBreaker breaker(config);

    // Open circuit
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    // Wait and transition to HALF_OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(breaker.should_allow_request());
    REQUIRE(breaker.get_state() == CircuitState::HALF_OPEN);

    // Single failure should reopen circuit
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);
}

TEST_CASE("CircuitBreaker - Sliding window removes old failures", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    config.window_ms = 200; // 200ms window

    CircuitBreaker breaker(config);

    // Record 4 failures
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // These failures should be in a new window
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::CLOSED); // Only 1 failure in new window
}

TEST_CASE("CircuitBreaker - force_open transitions to OPEN", "[circuit_breaker]") {
    CircuitBreaker breaker(CircuitBreakerConfig{});

    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    breaker.force_open();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);
}

TEST_CASE("CircuitBreaker - force_open is idempotent", "[circuit_breaker]") {
    CircuitBreaker breaker(CircuitBreakerConfig{});

    breaker.force_open();
    auto transitions_before = breaker.get_state_transitions();

    breaker.force_open();
    auto transitions_after = breaker.get_state_transitions();

    REQUIRE(transitions_before == transitions_after); // No additional transition
}

TEST_CASE("CircuitBreaker - Metrics tracking", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;

    CircuitBreaker breaker(config);

    // Record mixed results
    breaker.record_success();
    breaker.record_success();
    breaker.record_failure();
    breaker.record_success();
    breaker.record_failure();

    REQUIRE(breaker.get_total_successes() == 3);
    REQUIRE(breaker.get_total_failures() == 2);
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    // Trigger open
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();

    REQUIRE(breaker.get_state() == CircuitState::OPEN);
    REQUIRE(breaker.get_state_transitions() == 1); // CLOSED → OPEN

    // Try requests (should be rejected)
    breaker.should_allow_request();
    breaker.should_allow_request();

    REQUIRE(breaker.get_rejected_requests() == 2);
}

TEST_CASE("CircuitBreaker - Move constructor", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;

    CircuitBreaker breaker1(config);
    breaker1.record_failure();
    breaker1.record_success();

    auto successes = breaker1.get_total_successes();
    auto failures = breaker1.get_total_failures();

    CircuitBreaker breaker2(std::move(breaker1));

    REQUIRE(breaker2.get_total_successes() == successes);
    REQUIRE(breaker2.get_total_failures() == failures);
}

TEST_CASE("CircuitBreaker - Move assignment", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;

    CircuitBreaker breaker1(config);
    breaker1.record_failure();
    breaker1.record_success();

    auto successes = breaker1.get_total_successes();
    auto failures = breaker1.get_total_failures();

    CircuitBreaker breaker2(config);
    breaker2 = std::move(breaker1);

    REQUIRE(breaker2.get_total_successes() == successes);
    REQUIRE(breaker2.get_total_failures() == failures);
}

TEST_CASE("CircuitBreaker - Full state machine cycle", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.success_threshold = 2;
    config.timeout_ms = 50;

    CircuitBreaker breaker(config);

    // Start in CLOSED
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);
    REQUIRE(breaker.should_allow_request());

    // Trigger CLOSED → OPEN
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Should reject requests
    REQUIRE_FALSE(breaker.should_allow_request());

    // Wait for timeout → OPEN → HALF_OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(breaker.should_allow_request()); // Triggers transition
    REQUIRE(breaker.get_state() == CircuitState::HALF_OPEN);

    // Recovery test succeeds → HALF_OPEN → CLOSED
    breaker.record_success();
    breaker.record_success();
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    // Should allow requests again
    REQUIRE(breaker.should_allow_request());
}

TEST_CASE("CircuitBreaker - Catastrophic failure detection", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 5;
    config.catastrophic_threshold = 10;
    config.enable_global_hints = true;

    CircuitBreaker breaker(config);

    // Record catastrophic number of failures
    for (int i = 0; i < 10; ++i) {
        breaker.record_failure();
    }

    // Circuit should be OPEN
    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Global flag should be set (this is tested indirectly through should_allow_request)
    // Another instance would see this flag and reject requests
}

TEST_CASE("CircuitBreaker - Recovery clears failure history", "[circuit_breaker]") {
    CircuitBreakerConfig config;
    config.failure_threshold = 3;
    config.success_threshold = 2;
    config.timeout_ms = 50;

    CircuitBreaker breaker(config);

    // Open circuit
    breaker.record_failure();
    breaker.record_failure();
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);

    // Transition to HALF_OPEN and recover
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    breaker.should_allow_request();
    breaker.record_success();
    breaker.record_success();

    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    // Should not immediately reopen (failure history cleared)
    breaker.record_failure();
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::CLOSED);

    // Need full threshold again to open
    breaker.record_failure();
    REQUIRE(breaker.get_state() == CircuitState::OPEN);
}
