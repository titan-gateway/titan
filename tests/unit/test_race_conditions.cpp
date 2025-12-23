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

// Race Condition Tests
// Tests for thread-safety, concurrent validation, RCU semantics

#include <atomic>
#include <barrier>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"

using namespace titan::control;

// ============================================================================
// Test 1: Concurrent Validation (Read-Only)
// ============================================================================

TEST_CASE("Concurrent validation of same config (read-only)", "[race][security]") {
    Config config;

    // Create valid config
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.cors_configs["cors"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("cors");
    config.routes.push_back(route);

    ConfigValidator validator;

    // Validate from multiple threads concurrently
    constexpr int num_threads = 10;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    failure_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All validations should succeed
    REQUIRE(success_count == num_threads * iterations_per_thread);
    REQUIRE(failure_count == 0);
}

TEST_CASE("Concurrent validation of different configs", "[race][security]") {
    ConfigValidator validator;

    constexpr int num_threads = 10;
    constexpr int iterations_per_thread = 50;

    std::vector<std::thread> threads;
    std::atomic<int> total_validations{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                // Each thread creates its own config
                Config config;
                config.rate_limits["rate_" + std::to_string(thread_id)] = RateLimitConfig{};

                RouteConfig route;
                route.path = "/api/test" + std::to_string(thread_id);
                route.middleware.push_back("rate_" + std::to_string(thread_id));
                config.routes.push_back(route);

                auto result = validator.validate(config);
                if (result.valid) {
                    total_validations.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(total_validations == num_threads * iterations_per_thread);
}

TEST_CASE("Validation with high concurrency stress", "[race][security]") {
    Config config;

    // Large config
    for (int i = 0; i < 50; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
    }

    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 20; ++i) {
        route.middleware.push_back("rate_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;

    constexpr int num_threads = 20;
    constexpr int iterations_per_thread = 50;

    std::vector<std::thread> threads;
    std::atomic<bool> no_crashes{true};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            try {
                for (int j = 0; j < iterations_per_thread; ++j) {
                    auto result = validator.validate(config);
                    // Just verify we didn't crash
                    (void)result;
                }
            } catch (...) {
                no_crashes.store(false, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(no_crashes);
}

TEST_CASE("Concurrent validation with invalid config", "[race][security]") {
    Config config;

    // Invalid config (unknown middleware)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("nonexistent");
    config.routes.push_back(route);

    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> invalid_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (!result.valid) {
                    invalid_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // All validations should detect the error
    REQUIRE(invalid_count == num_threads * iterations_per_thread);
}

// ============================================================================
// Test 2: Fuzzy Matching Concurrency
// ============================================================================

TEST_CASE("Concurrent fuzzy matching for typos", "[race][security]") {
    Config config;

    // Create middleware
    for (int i = 0; i < 20; ++i) {
        config.rate_limits["middleware_" + std::to_string(i)] = RateLimitConfig{};
    }

    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations_per_thread = 50;

    std::vector<std::thread> threads;
    std::atomic<int> suggestions_found{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                // Typo: middlewre instead of middleware
                Config test_config = config;
                RouteConfig route;
                route.path = "/api/test";
                route.middleware.push_back("middlewre_5");
                test_config.routes.push_back(route);

                auto result = validator.validate(test_config);
                if (!result.valid && !result.errors.empty()) {
                    // Check if suggestion was provided
                    if (result.errors[0].find("Did you mean") != std::string::npos) {
                        suggestions_found.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Most should find suggestions
    REQUIRE(suggestions_found >= num_threads * iterations_per_thread * 0.9);
}

TEST_CASE("Fuzzy matching with concurrent config modifications", "[race][security]") {
    ConfigValidator validator;

    constexpr int num_threads = 4;
    constexpr int iterations = 25;

    std::vector<std::thread> threads;
    std::atomic<int> completed{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < iterations; ++j) {
                // Each thread creates unique config
                Config config;
                for (int k = 0; k < 10; ++k) {
                    config
                        .rate_limits["mw_t" + std::to_string(thread_id) + "_" + std::to_string(k)] =
                        RateLimitConfig{};
                }

                RouteConfig route;
                route.path = "/api/test";
                route.middleware.push_back("mw_typo");  // Should not match
                config.routes.push_back(route);

                auto result = validator.validate(config);
                // Just verify no crash
                (void)result;
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(completed == num_threads * iterations);
}

TEST_CASE("Levenshtein distance calculation race", "[race][security]") {
    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<bool> no_crashes{true};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            try {
                for (int j = 0; j < iterations_per_thread; ++j) {
                    Config config;
                    config.rate_limits["jwt_auth"] = RateLimitConfig{};

                    RouteConfig route;
                    route.path = "/api/test";
                    route.middleware.push_back("jvt_auht");  // Double typo
                    config.routes.push_back(route);

                    auto result = validator.validate(config);
                    (void)result;
                }
            } catch (...) {
                no_crashes.store(false, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(no_crashes);
}

// ============================================================================
// Test 3: Validator State Safety
// ============================================================================

TEST_CASE("Multiple validator instances concurrent operation", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    constexpr int num_threads = 10;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            // Each thread has its own validator
            ConfigValidator validator;
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(success_count == num_threads * iterations_per_thread);
}

TEST_CASE("Shared validator instance safety", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    // Single shared validator
    ConfigValidator validator;

    constexpr int num_threads = 10;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Validator should be stateless, so all should succeed
    REQUIRE(success_count == num_threads * iterations_per_thread);
}

// ============================================================================
// Test 4: Config Object Immutability
// ============================================================================

TEST_CASE("Read-only config access from multiple threads", "[race][security]") {
    Config config;

    for (int i = 0; i < 30; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
    }

    for (int i = 0; i < 10; ++i) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(i);
        route.middleware.push_back("rate_" + std::to_string(i));
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    constexpr int num_threads = 12;
    constexpr int iterations_per_thread = 50;

    std::vector<std::thread> threads;
    std::atomic<int> valid_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    valid_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(valid_count == num_threads * iterations_per_thread);
}

TEST_CASE("Config const-correctness verification", "[race][security]") {
    const Config config = []() {
        Config c;
        c.rate_limits["rate_limit"] = RateLimitConfig{};
        RouteConfig route;
        route.path = "/api/test";
        route.middleware.push_back("rate_limit");
        c.routes.push_back(route);
        return c;
    }();

    ConfigValidator validator;

    constexpr int num_threads = 8;
    std::vector<std::thread> threads;
    std::atomic<int> valid_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    valid_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(valid_count == num_threads * 100);
}

// ============================================================================
// Test 5: Barrier-Synchronized Concurrent Access
// ============================================================================

TEST_CASE("Synchronized burst validation", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;

    constexpr int num_threads = 10;
    std::barrier sync_point(num_threads);

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            // Wait for all threads to be ready
            sync_point.arrive_and_wait();

            // All threads validate at the same time
            auto result = validator.validate(config);
            if (result.valid) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(success_count == num_threads);
}

TEST_CASE("Burst validation with complex config", "[race][security]") {
    Config config;

    // Complex config
    for (int i = 0; i < 100; ++i) {
        config.rate_limits["middleware_" + std::to_string(i)] = RateLimitConfig{};
    }

    for (int i = 0; i < 50; ++i) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(i);
        for (int j = 0; j < 15; ++j) {
            route.middleware.push_back("middleware_" + std::to_string(j));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    constexpr int num_threads = 16;
    std::barrier sync_point(num_threads);

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            sync_point.arrive_and_wait();

            auto result = validator.validate(config);
            if (result.valid) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(success_count == num_threads);
}

// ============================================================================
// Test 6: Memory Safety Under Concurrency
// ============================================================================

TEST_CASE("No use-after-free with concurrent validation", "[race][security]") {
    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations = 100;

    std::vector<std::thread> threads;
    std::atomic<bool> no_crashes{true};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            try {
                for (int j = 0; j < iterations; ++j) {
                    // Create config in local scope
                    Config config;
                    config.rate_limits["rate"] = RateLimitConfig{};

                    RouteConfig route;
                    route.path = "/api/test";
                    route.middleware.push_back("rate");
                    config.routes.push_back(route);

                    auto result = validator.validate(config);
                    // Config goes out of scope here
                    (void)result;
                }
            } catch (...) {
                no_crashes.store(false, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(no_crashes);
}

TEST_CASE("Large string handling under concurrency", "[race][security]") {
    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations = 50;

    std::vector<std::thread> threads;
    std::atomic<int> completed{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                Config config;

                // Create middleware with max-length name
                std::string long_name(64, 'a');
                long_name[63] = '1';
                config.rate_limits[long_name] = RateLimitConfig{};

                RouteConfig route;
                route.path = "/api/test";
                route.middleware.push_back(long_name);
                config.routes.push_back(route);

                auto result = validator.validate(config);
                if (result.valid) {
                    completed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(completed == num_threads * iterations);
}

// ============================================================================
// Test 7: Error Handling Concurrency
// ============================================================================

TEST_CASE("Concurrent error detection and reporting", "[race][security]") {
    Config config;

    // Multiple errors in config
    RouteConfig route1;
    route1.path = "/api/route1";
    route1.middleware.push_back("nonexistent1");
    config.routes.push_back(route1);

    RouteConfig route2;
    route2.path = "/api/route2";
    route2.middleware.push_back("nonexistent2");
    config.routes.push_back(route2);

    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations = 100;

    std::vector<std::thread> threads;
    std::atomic<int> errors_detected{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                auto result = validator.validate(config);
                if (!result.valid && result.errors.size() == 2) {
                    errors_detected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(errors_detected == num_threads * iterations);
}

TEST_CASE("Warning generation thread safety", "[race][security]") {
    Config config;

    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_1");
    route.middleware.push_back("rate_2");  // Duplicate type (warning)
    config.routes.push_back(route);

    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations = 100;

    std::vector<std::thread> threads;
    std::atomic<int> warnings_found{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                auto result = validator.validate(config);
                if (result.valid && !result.warnings.empty()) {
                    warnings_found.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(warnings_found == num_threads * iterations);
}

// ============================================================================
// Test 8: Performance Under Concurrency
// ============================================================================

TEST_CASE("Validation latency under concurrent load", "[race][security]") {
    Config config;

    for (int i = 0; i < 50; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
    }

    for (int i = 0; i < 20; ++i) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(i);
        for (int j = 0; j < 10; ++j) {
            route.middleware.push_back("rate_" + std::to_string(j));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    constexpr int num_threads = 8;
    constexpr int iterations = 50;

    std::vector<std::thread> threads;
    std::atomic<int> completed{0};

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                auto result = validator.validate(config);
                (void)result;
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(completed == num_threads * iterations);
    REQUIRE(duration.count() < 10000);  // Should complete in <10s even with ASAN
}

TEST_CASE("Throughput with concurrent validators", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    constexpr int num_threads = 16;
    constexpr int iterations_per_thread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> total_validations{0};

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            ConfigValidator validator;
            for (int j = 0; j < iterations_per_thread; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    total_validations.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(total_validations == num_threads * iterations_per_thread);
    REQUIRE(duration.count() < 5000);  // Should complete in <5s
}

// ============================================================================
// Test 9: Edge Cases
// ============================================================================

TEST_CASE("Single-threaded baseline for comparison", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;

    int success_count = 0;
    for (int i = 0; i < 1000; ++i) {
        auto result = validator.validate(config);
        if (result.valid) {
            ++success_count;
        }
    }

    REQUIRE(success_count == 1000);
}

TEST_CASE("Two-thread minimal race test", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;

    std::barrier sync_point(2);
    std::atomic<int> success_count{0};

    std::thread t1([&]() {
        sync_point.arrive_and_wait();
        auto result = validator.validate(config);
        if (result.valid) {
            success_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread t2([&]() {
        sync_point.arrive_and_wait();
        auto result = validator.validate(config);
        if (result.valid) {
            success_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    REQUIRE(success_count == 2);
}

TEST_CASE("Maximum thread count stress test", "[race][security]") {
    Config config;
    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;

    // Use many threads (but don't exceed system limits)
    const int num_threads = std::min(32, static_cast<int>(std::thread::hardware_concurrency() * 4));
    constexpr int iterations = 50;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                auto result = validator.validate(config);
                if (result.valid) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    REQUIRE(success_count == num_threads * iterations);
}
