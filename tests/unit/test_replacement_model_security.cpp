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

// REPLACEMENT Model Security Tests
// Tests for type spoofing, collision detection, replacement semantics security

#include <catch2/catch_test_macros.hpp>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"

using namespace titan::control;

// ============================================================================
// Test 1: Type Spoofing
// ============================================================================

TEST_CASE("Middleware name collision across types is allowed", "[replacement_model][security]") {
    Config config;

    // Same name "auth" in different middleware types
    config.rate_limits["auth"] = RateLimitConfig{};
    config.cors_configs["auth"] = CorsConfig{};
    config.transform_configs["auth"] = TransformConfig{};

    // Route references "auth" - which type is it?
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be valid (no error for cross-type name collision)
    REQUIRE(result.valid);
}

TEST_CASE("Same name in rate_limits and cors_configs", "[replacement_model][security]") {
    Config config;

    config.rate_limits["shared_name"] = RateLimitConfig{};
    config.cors_configs["shared_name"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("shared_name");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Type resolution priority order", "[replacement_model][security]") {
    Config config;

    // Same name in all types
    config.rate_limits["middleware"] = RateLimitConfig{};
    config.cors_configs["middleware"] = CorsConfig{};
    config.transform_configs["middleware"] = TransformConfig{};
    config.compression_configs["middleware"] = CompressionConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("middleware");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be valid (validator doesn't enforce type priority)
    REQUIRE(result.valid);
}

TEST_CASE("Type inference with similar names", "[replacement_model][security]") {
    Config config;

    // Similar but distinct names
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.rate_limits["rate_limiter"] = RateLimitConfig{};
    config.rate_limits["rate_limiting"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("rate_limiter");
    route.middleware.push_back("rate_limiting");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should have warning about multiple rate_limits
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Spoofing attempt with special characters in name", "[replacement_model][security]") {
    Config config;

    // Names that might cause confusion (but are rejected by validation)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate.limit");  // Invalid: contains '.'
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (invalid character)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Case sensitivity in middleware names", "[replacement_model][security]") {
    Config config;

    // Different case variations
    config.rate_limits["JWT_Auth"] = RateLimitConfig{};
    config.cors_configs["jwt_auth"] = CorsConfig{};
    config.transform_configs["Jwt_Auth"] = TransformConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("JWT_Auth");  // Matches rate_limits
    route.middleware.push_back("jwt_auth");  // Matches cors_configs
    route.middleware.push_back("Jwt_Auth");  // Matches transform_configs
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Numeric suffix collision detection", "[replacement_model][security]") {
    Config config;

    // Similar names with numeric suffixes
    config.rate_limits["rate_limit_1"] = RateLimitConfig{};
    config.rate_limits["rate_limit_2"] = RateLimitConfig{};
    config.rate_limits["rate_limit_10"] = RateLimitConfig{};  // Could be mistaken for 1 + 0

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit_1");
    route.middleware.push_back("rate_limit_2");
    route.middleware.push_back("rate_limit_10");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn about multiple rate_limits
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Underscore vs hyphen collision", "[replacement_model][security]") {
    Config config;

    // Names differing only in separator
    config.rate_limits["rate_limit"] = RateLimitConfig{};
    config.cors_configs["rate-limit"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");  // Matches rate_limits
    route.middleware.push_back("rate-limit");  // Matches cors_configs
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

// ============================================================================
// Test 2: Collision Detection
// ============================================================================

TEST_CASE("Duplicate names within same type map", "[replacement_model][security]") {
    Config config;

    // Duplicate insertion (last write wins in map)
    config.rate_limits["duplicate"] = RateLimitConfig{};
    config.rate_limits["duplicate"] = RateLimitConfig{};  // Overwrites previous

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("duplicate");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(config.rate_limits.size() == 1);  // Only one entry (map behavior)
}

TEST_CASE("Cross-type name collision with validation", "[replacement_model][security]") {
    Config config;

    // Same name in multiple types
    config.rate_limits["shared"] = RateLimitConfig{};
    config.cors_configs["shared"] = CorsConfig{};

    // Multiple routes reference the same name
    RouteConfig route1;
    route1.path = "/api/route1";
    route1.middleware.push_back("shared");
    config.routes.push_back(route1);

    RouteConfig route2;
    route2.path = "/api/route2";
    route2.middleware.push_back("shared");
    config.routes.push_back(route2);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Hash collision simulation (similar hash values)", "[replacement_model][security]") {
    Config config;

    // Create many middleware with names that might hash similarly
    // (depends on hash function implementation)
    for (int i = 0; i < 50; ++i) {
        std::string name = "middleware_" + std::string(10, 'a' + (i % 26));
        config.rate_limits[name] = RateLimitConfig{};
    }

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("middleware_aaaaaaaaaa");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Name similarity exploitation with Levenshtein", "[replacement_model][security]") {
    Config config;

    // Create middleware with very similar names
    config.rate_limits["jwt_auth"] = RateLimitConfig{};
    config.rate_limits["jvt_auth"] = RateLimitConfig{};  // Typo: j→v
    config.rate_limits["jwt_auht"] = RateLimitConfig{};  // Typo: auth→auht

    // Route with typo - should suggest multiple similar names
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("jwt_autj");  // Typo: should find jwt_auth
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);  // Unknown middleware
    REQUIRE(result.errors.size() == 1);
    // Should suggest jwt_auth (closest match)
}

TEST_CASE("Empty string collision (should be rejected)", "[replacement_model][security]") {
    Config config;

    config.rate_limits[""] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE_FALSE(result.valid);

    bool found_empty_error = false;
    for (const auto& error : result.errors) {
        if (error.find("cannot be empty") != std::string::npos ||
            error.find("Unknown middleware ''") != std::string::npos) {
            found_empty_error = true;
            break;
        }
    }
    REQUIRE(found_empty_error);
}

TEST_CASE("Maximum length name collision", "[replacement_model][security]") {
    Config config;

    // Two 64-char names differing only in last char
    std::string name1(64, 'a');
    name1[63] = '1';
    std::string name2(64, 'a');
    name2[63] = '2';

    config.rate_limits[name1] = RateLimitConfig{};
    config.rate_limits[name2] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(name1);
    route.middleware.push_back(name2);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn about multiple rate_limits
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Unicode normalization collision (ASCII only)", "[replacement_model][security]") {
    Config config;

    // Since we only allow [a-zA-Z0-9_-], Unicode collision is not possible
    // This test verifies that Unicode characters are rejected

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("jwt_auth_café");  // Contains Unicode
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (invalid character)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Whitespace collision detection", "[replacement_model][security]") {
    Config config;

    // Names with leading/trailing whitespace (should be rejected)
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(" jwt_auth");  // Leading space
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (space is invalid character)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 3: REPLACEMENT Model Correctness
// ============================================================================

TEST_CASE("First middleware of type wins (REPLACEMENT semantics)",
          "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_limit_1"] = RateLimitConfig{};
    config.rate_limits["rate_limit_2"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit_1");  // First
    route.middleware.push_back("rate_limit_2");  // Second (should be ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn that only first executes
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());

    bool found_replacement_warning = false;
    for (const auto& warning : result.warnings) {
        if (warning.find("Multiple middleware of same type") != std::string::npos &&
            warning.find("REPLACEMENT") != std::string::npos) {
            found_replacement_warning = true;
            break;
        }
    }
    REQUIRE(found_replacement_warning);
}

TEST_CASE("Subsequent middleware of same type ignored", "[replacement_model][security]") {
    Config config;

    config.cors_configs["cors_1"] = CorsConfig{};
    config.cors_configs["cors_2"] = CorsConfig{};
    config.cors_configs["cors_3"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("cors_1");
    route.middleware.push_back("cors_2");
    route.middleware.push_back("cors_3");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn about duplicates
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Warning generation for duplicate types", "[replacement_model][security]") {
    Config config;

    config.transform_configs["transform_1"] = TransformConfig{};
    config.transform_configs["transform_2"] = TransformConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("transform_1");
    route.middleware.push_back("transform_2");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 1);
}

TEST_CASE("Multi-type chain (only first of each type executes)", "[replacement_model][security]") {
    Config config;

    // 2 of each type
    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};
    config.cors_configs["cors_1"] = CorsConfig{};
    config.cors_configs["cors_2"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_1");
    route.middleware.push_back("cors_1");
    route.middleware.push_back("rate_2");  // Duplicate type
    route.middleware.push_back("cors_2");  // Duplicate type
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should have 2 warnings (rate_limit and cors duplicates)
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 2);
}

TEST_CASE("Same middleware instance referenced multiple times", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    route.middleware.push_back("rate_limit");  // Same instance again
    route.middleware.push_back("rate_limit");  // And again
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn about duplicate types
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Interleaved types with duplicates", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};
    config.cors_configs["cors_1"] = CorsConfig{};
    config.cors_configs["cors_2"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    // Pattern: rate, cors, rate, cors
    route.middleware.push_back("rate_1");
    route.middleware.push_back("cors_1");
    route.middleware.push_back("rate_2");
    route.middleware.push_back("cors_2");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should have warnings for both types
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 2);
}

TEST_CASE("REPLACEMENT semantics across multiple routes", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};

    // Route 1: rate_1, rate_2 (warning)
    RouteConfig route1;
    route1.path = "/api/route1";
    route1.middleware.push_back("rate_1");
    route1.middleware.push_back("rate_2");
    config.routes.push_back(route1);

    // Route 2: rate_2, rate_1 (different order, still warning)
    RouteConfig route2;
    route2.path = "/api/route2";
    route2.middleware.push_back("rate_2");
    route2.middleware.push_back("rate_1");
    config.routes.push_back(route2);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should have 2 warnings (one per route)
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 2);
}

TEST_CASE("No warning when only one middleware per type", "[replacement_model][security]") {
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

    // Should be valid with no warnings
    REQUIRE(result.valid);
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.empty());
}

TEST_CASE("Empty chain has no REPLACEMENT warnings", "[replacement_model][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    // No middleware
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.warnings.empty());
}

TEST_CASE("Single middleware has no REPLACEMENT warnings", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_limit");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
    REQUIRE(result.warnings.empty());
}

// ============================================================================
// Test 4: Security Bypass via REPLACEMENT
// ============================================================================

TEST_CASE("Auth bypass by registering duplicate auth middleware", "[replacement_model][security]") {
    Config config;

    // Legitimate auth
    config.transform_configs["strict_auth"] = TransformConfig{};
    // Weak auth (should be ignored due to REPLACEMENT)
    config.transform_configs["weak_auth"] = TransformConfig{};

    RouteConfig route;
    route.path = "/api/secure";
    route.middleware.push_back("weak_auth");    // First (wins)
    route.middleware.push_back("strict_auth");  // Second (ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn that only first executes (potential security issue)
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Rate limit bypass by duplicate registration", "[replacement_model][security]") {
    Config config;

    config.rate_limits["strict_limit"] = RateLimitConfig{};
    config.rate_limits["lenient_limit"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("lenient_limit");  // First (wins)
    route.middleware.push_back("strict_limit");   // Second (ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Warns that only first executes
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("CORS bypass attempt with duplicate policies", "[replacement_model][security]") {
    Config config;

    config.cors_configs["permissive_cors"] = CorsConfig{};
    config.cors_configs["restrictive_cors"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("permissive_cors");   // First (wins)
    route.middleware.push_back("restrictive_cors");  // Second (ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Transform bypass with duplicate transforms", "[replacement_model][security]") {
    Config config;

    config.transform_configs["safe_transform"] = TransformConfig{};
    config.transform_configs["unsafe_transform"] = TransformConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("unsafe_transform");  // First (wins)
    route.middleware.push_back("safe_transform");    // Second (ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Compression bypass with duplicate configs", "[replacement_model][security]") {
    Config config;

    config.compression_configs["high_compression"] = CompressionConfig{};
    config.compression_configs["no_compression"] = CompressionConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("no_compression");    // First (wins)
    route.middleware.push_back("high_compression");  // Second (ignored)
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

// ============================================================================
// Test 5: Edge Cases
// ============================================================================

TEST_CASE("All middleware are of same type", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};
    config.rate_limits["rate_3"] = RateLimitConfig{};
    config.rate_limits["rate_4"] = RateLimitConfig{};
    config.rate_limits["rate_5"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    for (int i = 1; i <= 5; ++i) {
        route.middleware.push_back("rate_" + std::to_string(i));
    }
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn about all duplicates
    REQUIRE(result.errors.empty());
    REQUIRE_FALSE(result.warnings.empty());
}

TEST_CASE("Interleaved types (A, B, A, B, A)", "[replacement_model][security]") {
    Config config;

    config.rate_limits["rate_1"] = RateLimitConfig{};
    config.rate_limits["rate_2"] = RateLimitConfig{};
    config.rate_limits["rate_3"] = RateLimitConfig{};
    config.cors_configs["cors_1"] = CorsConfig{};
    config.cors_configs["cors_2"] = CorsConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate_1");
    route.middleware.push_back("cors_1");
    route.middleware.push_back("rate_2");
    route.middleware.push_back("cors_2");
    route.middleware.push_back("rate_3");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should warn for both types
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 2);
}

TEST_CASE("Maximum middleware chain with all different types", "[replacement_model][security]") {
    Config config;

    // Create 5 of each type (20 total)
    for (int i = 0; i < 5; ++i) {
        config.rate_limits["rate_" + std::to_string(i)] = RateLimitConfig{};
        config.cors_configs["cors_" + std::to_string(i)] = CorsConfig{};
        config.transform_configs["transform_" + std::to_string(i)] = TransformConfig{};
        config.compression_configs["compress_" + std::to_string(i)] = CompressionConfig{};
    }

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

    // Should warn for all 4 types (duplicates)
    REQUIRE(result.errors.empty());
    REQUIRE(result.warnings.size() >= 4);
}

TEST_CASE("Route with only unknown middleware", "[replacement_model][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("nonexistent_middleware");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should error (unknown middleware)
    REQUIRE_FALSE(result.valid);
    REQUIRE_FALSE(result.errors.empty());
}
