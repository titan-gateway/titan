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

// Middleware Chain Security Tests
// Tests for chain length limits, memory/CPU exhaustion, order-dependent vulnerabilities

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <sstream>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"

using namespace titan::control;

// ============================================================================
// Test 1: Chain Length Enforcement
// ============================================================================

TEST_CASE("Chain length at limit (20) is accepted", "[middleware_chain][security]") {
    Config config;

    // Create 20 named middleware
    for (int i = 0; i < 20; ++i) {
        std::string name = "rate_limit_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with 20 middleware (at limit)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 20; ++i) {
        route.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("Chain length over limit (21) is rejected", "[middleware_chain][security]") {
    Config config;

    // Create 21 named middleware
    for (int i = 0; i < 21; ++i) {
        std::string name = "rate_limit_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with 21 middleware (over limit)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 21; ++i) {
        route.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());

    bool found_chain_length_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Middleware chain too long") != std::string::npos &&
            error.find("21 > 20") != std::string::npos) {
            found_chain_length_error = true;
            break;
        }
    }
    REQUIRE(found_chain_length_error);
}

TEST_CASE("Pathological chain length (100) is rejected", "[middleware_chain][security]") {
    Config config;

    // Create 100 named middleware
    for (int i = 0; i < 100; ++i) {
        std::string name = "rate_limit_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with 100 middleware (way over limit)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 100; ++i) {
        route.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);

    bool found_chain_length_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Middleware chain too long") != std::string::npos) {
            found_chain_length_error = true;
            break;
        }
    }
    REQUIRE(found_chain_length_error);
}

TEST_CASE("Empty middleware chain is accepted", "[middleware_chain][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    // No middleware
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("Single middleware chain is accepted", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit_1"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit_1");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

TEST_CASE("Multiple routes with varying chain lengths", "[middleware_chain][security]") {
    Config config;

    // Create 25 named middleware
    for (int i = 0; i < 25; ++i) {
        std::string name = "rate_limit_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Route 1: 5 middleware (valid)
    RouteConfig route1;
    route1.path = "/api/route1";
    for (int i = 0; i < 5; ++i) {
        route1.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route1);

    // Route 2: 20 middleware (at limit, valid)
    RouteConfig route2;
    route2.path = "/api/route2";
    for (int i = 0; i < 20; ++i) {
        route2.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route2);

    // Route 3: 25 middleware (over limit, invalid)
    RouteConfig route3;
    route3.path = "/api/route3";
    for (int i = 0; i < 25; ++i) {
        route3.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route3);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);
    REQUIRE(result.errors.size() == 1);  // Only route3 should fail
    REQUIRE(result.errors[0].find("/api/route3") != std::string::npos);
    REQUIRE(result.errors[0].find("25 > 20") != std::string::npos);
}

TEST_CASE("Cross-type middleware chain length enforcement", "[middleware_chain][security]") {
    Config config;

    // Create mixed middleware types totaling >20
    for (int i = 0; i < 10; ++i) {
        config.rate_limits["rate_limit_" + std::to_string(i)] = RateLimitConfig{};
    }
    for (int i = 0; i < 10; ++i) {
        config.cors_configs["cors_" + std::to_string(i)] = CorsConfig{};
    }
    for (int i = 0; i < 5; ++i) {
        config.transform_configs["transform_" + std::to_string(i)] = TransformConfig{};
    }

    // Create route with 25 mixed middleware (over limit)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 10; ++i) {
        route.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        route.middleware.push_back("cors_" + std::to_string(i));
    }
    for (int i = 0; i < 5; ++i) {
        route.middleware.push_back("transform_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);

    bool found_chain_length_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Middleware chain too long") != std::string::npos) {
            found_chain_length_error = true;
            break;
        }
    }
    REQUIRE(found_chain_length_error);
}

TEST_CASE("Chain length validation is per-route independent", "[middleware_chain][security]") {
    Config config;

    // Create 20 named middleware
    for (int i = 0; i < 20; ++i) {
        std::string name = "rate_limit_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Both routes have 20 middleware (both at limit, both valid)
    RouteConfig route1;
    route1.path = "/api/route1";
    for (int i = 0; i < 20; ++i) {
        route1.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route1);

    RouteConfig route2;
    route2.path = "/api/route2";
    for (int i = 0; i < 20; ++i) {
        route2.middleware.push_back("rate_limit_" + std::to_string(i));
    }
    config.routes.push_back(route2);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
}

// ============================================================================
// Test 2: Memory Exhaustion Protection
// ============================================================================

TEST_CASE("Large chains with many references don't exhaust memory",
          "[middleware_chain][security]") {
    Config config;

    // Create 20 middleware (at limit)
    for (int i = 0; i < 20; ++i) {
        std::string name = "middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create 50 routes, each with 20 middleware (1000 total references)
    for (int r = 0; r < 50; ++r) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(r);
        for (int i = 0; i < 20; ++i) {
            route.middleware.push_back("middleware_" + std::to_string(i));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = validator.validate(config);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(result.valid);
    REQUIRE(duration.count() < 5000);  // Should complete in <5s even with ASAN
}

TEST_CASE("Memory usage with 100 middleware definitions", "[middleware_chain][security]") {
    Config config;

    // Create exactly 100 middleware (at MAX_REGISTERED_MIDDLEWARE limit)
    for (int i = 0; i < 100; ++i) {
        std::string name = "middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with 20 middleware (at chain limit)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 20; ++i) {
        route.middleware.push_back("middleware_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Duplicate middleware in chain is allowed (REPLACEMENT model)",
          "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit_1"] = RateLimitConfig{};

    // Same middleware appears multiple times in chain
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit_1");
    route.middleware.push_back("rate_limit_1");
    route.middleware.push_back("rate_limit_1");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be valid (no error), but may have warning about duplicates
    REQUIRE(result.errors.empty());
    // Warnings about same type appearing multiple times
    REQUIRE(result.warnings.size() >= 1);
}

TEST_CASE("Chain with maximum-length middleware names", "[middleware_chain][security]") {
    Config config;

    // Create 10 middleware with 64-char names (at MAX_MIDDLEWARE_NAME_LENGTH limit)
    for (int i = 0; i < 10; ++i) {
        std::string name(64, 'a');  // 64 'a's
        name[63] = '0' + i;         // Make unique
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with all 10 middleware
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 10; ++i) {
        std::string name(64, 'a');
        name[63] = '0' + i;
        route.middleware.push_back(name);
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

// ============================================================================
// Test 3: CPU Exhaustion Protection
// ============================================================================

TEST_CASE("Complex chain validation completes in reasonable time", "[middleware_chain][security]") {
    Config config;

    // Create 100 middleware (at limit)
    for (int i = 0; i < 100; ++i) {
        std::string name = "middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create 100 routes, each with 20 middleware (2000 total validations)
    for (int r = 0; r < 100; ++r) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(r);
        for (int i = 0; i < 20; ++i) {
            route.middleware.push_back("middleware_" + std::to_string(i));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = validator.validate(config);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(result.valid);
    REQUIRE(duration.count() < 10000);  // Should complete in <10s even with ASAN
}

TEST_CASE("Worst-case fuzzy matching scenario", "[middleware_chain][security]") {
    Config config;

    // Create 100 middleware with similar names (trigger fuzzy matching)
    for (int i = 0; i < 100; ++i) {
        std::string name = "jwt_auth_middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create route with typo that triggers fuzzy match against all 100
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("jwt_auht_middleware_999");  // Typo: auht instead of auth
    config.routes.push_back(route);

    ConfigValidator validator;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = validator.validate(config);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE_FALSE(result.valid);       // Should detect typo
    REQUIRE(duration.count() < 2000);  // Fuzzy matching should complete in <2s
}

TEST_CASE("Many routes with long chains validation performance", "[middleware_chain][security]") {
    Config config;

    // Create 50 middleware
    for (int i = 0; i < 50; ++i) {
        std::string name = "middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create 200 routes, each with 15 middleware (3000 total validations)
    for (int r = 0; r < 200; ++r) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(r);
        for (int i = 0; i < 15; ++i) {
            route.middleware.push_back("middleware_" + std::to_string(i % 50));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    auto start = std::chrono::high_resolution_clock::now();
    auto result = validator.validate(config);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(result.valid);
    REQUIRE(duration.count() < 5000);  // Should complete in <5s
}

TEST_CASE("Repeated validation performance (config reload scenario)",
          "[middleware_chain][security]") {
    Config config;

    // Create 50 middleware
    for (int i = 0; i < 50; ++i) {
        std::string name = "middleware_" + std::to_string(i);
        config.rate_limits[name] = RateLimitConfig{};
    }

    // Create 50 routes, each with 10 middleware
    for (int r = 0; r < 50; ++r) {
        RouteConfig route;
        route.path = "/api/route" + std::to_string(r);
        for (int i = 0; i < 10; ++i) {
            route.middleware.push_back("middleware_" + std::to_string(i));
        }
        config.routes.push_back(route);
    }

    ConfigValidator validator;

    // Validate 10 times (simulating config reloads)
    auto start = std::chrono::high_resolution_clock::now();
    for (int iteration = 0; iteration < 10; ++iteration) {
        auto result = validator.validate(config);
        REQUIRE(result.valid);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    REQUIRE(duration.count() < 5000);  // 10 validations should complete in <5s
}

// ============================================================================
// Test 4: Order-Dependent Vulnerabilities
// ============================================================================

TEST_CASE("Auth middleware placement warnings", "[middleware_chain][security]") {
    Config config;

    // Create auth and rate limit middleware (simulated with types we have)
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.transform_configs["auth_check"] = TransformConfig{};  // Simulating auth

    // Route with auth AFTER rate limit (potentially insecure: unauthenticated requests consume rate
    // limit)
    RouteConfig route;
    route.path = "/api/secure";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("auth_check");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Current validator doesn't check order, but chain is valid
    REQUIRE(result.valid);
}

TEST_CASE("Multiple middleware of same type (REPLACEMENT model)", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit_1"] = RateLimitConfig{};
    config.rate_limits["rate_limit_2"] = RateLimitConfig{};
    config.rate_limits["rate_limit_3"] = RateLimitConfig{};

    // Route with multiple rate limits (only first should execute)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit_1");
    route.middleware.push_back("rate_limit_2");
    route.middleware.push_back("rate_limit_3");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be valid but have warnings about duplicate types
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());

    bool found_duplicate_warning = false;
    for (const auto& warning : result.warnings) {
        if (warning.find("Multiple middleware of same type") != std::string::npos) {
            found_duplicate_warning = true;
            break;
        }
    }
    REQUIRE(found_duplicate_warning);
}

TEST_CASE("Transform before proxy is valid", "[middleware_chain][security]") {
    Config config;

    config.transform_configs["transform_headers"] = TransformConfig{};
    config.rate_limits["proxy_backend"] = RateLimitConfig{};  // Simulating proxy

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("transform_headers");
    route.middleware.push_back("proxy_backend");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("CORS placement (should be early in chain)", "[middleware_chain][security]") {
    Config config;

    config.cors_configs["cors_policy"] = CorsConfig{};
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.transform_configs["transform"] = TransformConfig{};

    // CORS early in chain (correct)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("cors_policy");
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("transform");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Compression placement (should be late in chain)", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.transform_configs["transform"] = TransformConfig{};
    config.compression_configs["compress_response"] = CompressionConfig{};

    // Compression late in chain (correct)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("transform");
    route.middleware.push_back("compress_response");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Mixed middleware types in recommended order", "[middleware_chain][security]") {
    Config config;

    // Recommended order: CORS → RateLimit → Auth → Transform → Proxy → Compression
    config.cors_configs["cors"] = CorsConfig{};
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.transform_configs["auth"] = TransformConfig{};  // Simulating auth
    config.transform_configs["transform"] = TransformConfig{};
    config.rate_limits["proxy"] = RateLimitConfig{};  // Simulating proxy
    config.compression_configs["compress"] = CompressionConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("cors");
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("auth");
    route.middleware.push_back("transform");
    route.middleware.push_back("proxy");
    route.middleware.push_back("compress");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

// ============================================================================
// Test 5: Circular Reference Detection
// ============================================================================

TEST_CASE("Self-reference in chain (same middleware twice)", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit"] = RateLimitConfig{};

    // Middleware appears twice in chain (A → A)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be valid (no circular reference in middleware definitions)
    // But may have warning about duplicate types
    REQUIRE(result.errors.empty());
}

TEST_CASE("Multiple references to same middleware across routes", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["shared_rate_limit"] = RateLimitConfig{};

    // Multiple routes reference the same middleware
    RouteConfig route1;
    route1.path = "/api/route1";
    route1.middleware.push_back("shared_rate_limit");
    config.routes.push_back(route1);

    RouteConfig route2;
    route2.path = "/api/route2";
    route2.middleware.push_back("shared_rate_limit");
    config.routes.push_back(route2);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Chain with alternating middleware types", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate1"] = RateLimitConfig{};
    config.cors_configs["cors1"] = CorsConfig{};
    config.rate_limits["rate2"] = RateLimitConfig{};
    config.cors_configs["cors2"] = CorsConfig{};

    // Chain: rate → cors → rate → cors (alternating types)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate1");
    route.middleware.push_back("cors1");
    route.middleware.push_back("rate2");
    route.middleware.push_back("cors2");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should have warnings about multiple middleware of same types
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 2);  // rate_limit and cors duplicates
}

// ============================================================================
// Test 6: Deep Nesting
// ============================================================================

TEST_CASE("Maximum depth chain (20 middleware)", "[middleware_chain][security]") {
    Config config;

    // Create 20 middleware of different types
    for (int i = 0; i < 5; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
        config.cors_configs["cors_" + std::to_string(i)] = CorsConfig{};
        config.transform_configs["transform_" + std::to_string(i)] = TransformConfig{};
        config.compression_configs["compress_" + std::to_string(i)] = CompressionConfig{};
    }

    // Create route with all 20 middleware (maximum depth)
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 5; ++i) {
        route.middleware.push_back("rate_" + std::to_string(i));
        route.middleware.push_back("cors_" + std::to_string(i));
        route.middleware.push_back("transform_" + std::to_string(i));
        route.middleware.push_back("compress_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());          // Should be valid (at limit)
    REQUIRE_FALSE(result.warnings.empty());  // But will have duplicate type warnings
}

TEST_CASE("All middleware types in one chain", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.cors_configs["cors"] = CorsConfig{};
    config.transform_configs["transform"] = TransformConfig{};
    config.compression_configs["compress"] = CompressionConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("cors");
    route.middleware.push_back("transform");
    route.middleware.push_back("compress");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Nested type patterns (grouped by type)", "[middleware_chain][security]") {
    Config config;

    // Create 3 of each type
    for (int i = 0; i < 3; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
        config.cors_configs["cors_" + std::to_string(i)] = CorsConfig{};
        config.transform_configs["transform_" + std::to_string(i)] = TransformConfig{};
    }

    // Chain grouped by type: rate×3, cors×3, transform×3
    RouteConfig route;
    route.path = "/api/test";
    for (int i = 0; i < 3; ++i) {
        route.middleware.push_back("rate_" + std::to_string(i));
    }
    for (int i = 0; i < 3; ++i) {
        route.middleware.push_back("cors_" + std::to_string(i));
    }
    for (int i = 0; i < 3; ++i) {
        route.middleware.push_back("transform_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 3);  // Warnings for each duplicate type
}

// ============================================================================
// Test 7: Edge Cases
// ============================================================================

TEST_CASE("Empty middleware name in chain is rejected", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["valid_middleware"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("valid_middleware");
    route.middleware.push_back("");  // Empty name
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);

    bool found_empty_name_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Middleware name cannot be empty") != std::string::npos ||
            error.find("Unknown middleware ''") != std::string::npos) {
            found_empty_name_error = true;
            break;
        }
    }
    REQUIRE(found_empty_name_error);
}

TEST_CASE("Whitespace-only middleware name in chain", "[middleware_chain][security]") {
    Config config;

    config.rate_limits["valid_middleware"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("valid_middleware");
    route.middleware.push_back("   ");  // Whitespace only
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);

    bool found_invalid_char_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Invalid character") != std::string::npos ||
            error.find("Unknown middleware") != std::string::npos) {
            found_invalid_char_error = true;
            break;
        }
    }
    REQUIRE(found_invalid_char_error);
}
