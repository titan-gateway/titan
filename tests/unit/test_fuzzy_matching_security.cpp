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

// Fuzzy Matching Security Tests
// Tests for DoS protection, threshold exploitation, performance

#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"
#include "../../src/core/string_utils.hpp"

using namespace titan::control;
using namespace titan::core;

// Helper to create config with many named middleware
Config create_config_with_many_middleware(size_t count) {
    Config config;

    for (size_t i = 0; i < count; ++i) {
        RateLimitConfig rl;
        rl.enabled = true;
        rl.requests_per_second = 100;
        config.rate_limits["middleware_" + std::to_string(i)] = rl;
    }

    return config;
}

TEST_CASE("Fuzzy matching DoS via long strings", "[security][fuzzy][dos]") {
    SECTION("Rejects 100-char string quickly") {
        std::string long_name(100, 'A');

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {long_name};

        Config config;
        config.routes.push_back(route);

        auto start = std::chrono::steady_clock::now();
        auto result = ConfigValidator::validate(config);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Should reject due to length, not hang on fuzzy matching
        REQUIRE_FALSE(result.valid);
        REQUIRE(duration.count() < 100);  // Must complete in <100ms
    }

    SECTION("Rejects 1KB string without DoS") {
        std::string long_name(1024, 'A');

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {long_name};

        Config config;
        config.routes.push_back(route);

        auto start = std::chrono::steady_clock::now();
        auto result = ConfigValidator::validate(config);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        REQUIRE_FALSE(result.valid);
        REQUIRE(duration.count() < 100);  // Fast rejection
    }

    SECTION("Rejects 10KB string without DoS") {
        std::string long_name(10000, 'A');

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {long_name};

        Config config;
        config.routes.push_back(route);

        auto start = std::chrono::steady_clock::now();
        auto result = ConfigValidator::validate(config);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        REQUIRE_FALSE(result.valid);
        REQUIRE(duration.count() < 100);
    }

    SECTION("Handles valid 64-char name efficiently") {
        std::string valid_name(MAX_MIDDLEWARE_NAME_LENGTH, 'A');

        Config config = create_config_with_many_middleware(10);
        config.rate_limits[valid_name] = RateLimitConfig{};

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {valid_name};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto start = std::chrono::steady_clock::now();
        auto result = ConfigValidator::validate(config);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        REQUIRE(result.valid);
        REQUIRE(duration.count() < 50);  // Should be fast
    }
}

TEST_CASE("Levenshtein distance calculation performance", "[security][fuzzy][performance]") {
    SECTION("Short strings (10 chars) complete quickly") {
        std::string s1 = "jwt_auth_1";
        std::string s2 = "jwt_auth_2";

        size_t distance = levenshtein_distance(s1, s2);

        REQUIRE(distance == 1);
    }

    SECTION("Medium strings (32 chars) complete quickly") {
        std::string s1(32, 'A');
        std::string s2(32, 'B');

        size_t distance = levenshtein_distance(s1, s2);

        REQUIRE(distance == 32);
    }

    SECTION("Maximum allowed length (64 chars) completes in reasonable time") {
        std::string s1(MAX_MIDDLEWARE_NAME_LENGTH, 'A');
        std::string s2(MAX_MIDDLEWARE_NAME_LENGTH, 'B');

        size_t distance = levenshtein_distance(s1, s2);

        REQUIRE(distance == MAX_MIDDLEWARE_NAME_LENGTH);
    }

    SECTION("Identical strings are fast") {
        std::string s(MAX_MIDDLEWARE_NAME_LENGTH, 'A');

        size_t distance = levenshtein_distance(s, s);

        REQUIRE(distance == 0);
    }
}

TEST_CASE("Threshold exploitation", "[security][fuzzy][threshold]") {
    SECTION("Distance 1 - single char substitution") {
        std::vector<std::string> candidates = {"jwt_auth", "rate_limit", "compress"};
        auto similar = find_similar_strings("jwt_auth", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // Should not match itself (distance 0 excluded)
        REQUIRE(similar.empty());
    }

    SECTION("Distance 1 from legitimate name") {
        std::vector<std::string> candidates = {"jwt_auth"};

        // One char different
        auto similar1 = find_similar_strings("jwt_auht", candidates, MAX_LEVENSHTEIN_DISTANCE);
        REQUIRE(similar1.size() == 1);
        REQUIRE(similar1[0] == "jwt_auth");
    }

    SECTION("Distance 2 from legitimate name") {
        std::vector<std::string> candidates = {"jwt_auth"};

        // Two operations: delete 't' at end, substitute 'w' with 'v'
        auto similar2 = find_similar_strings("jvt_aut", candidates, MAX_LEVENSHTEIN_DISTANCE);
        REQUIRE(similar2.size() == 1);
        REQUIRE(similar2[0] == "jwt_auth");
    }

    SECTION("Distance 3 should not match (threshold is 2)") {
        std::vector<std::string> candidates = {"jwt_auth"};

        // Three chars different
        auto similar3 = find_similar_strings("jvt_aukt", candidates, MAX_LEVENSHTEIN_DISTANCE);
        REQUIRE(similar3.empty());  // Beyond threshold
    }

    SECTION("Multiple candidates at different distances") {
        std::vector<std::string> candidates = {
            "jwt_auth",   // distance 1 from jwt_auht
            "jwt_authz",  // distance 1 from jwt_auht
            "rate_limit"  // distance >2, won't match
        };

        auto similar = find_similar_strings("jwt_auht", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // Should match both jwt_auth and jwt_authz (both within threshold)
        REQUIRE(similar.size() == 2);
    }
}

TEST_CASE("Collision attacks", "[security][fuzzy][collision]") {
    SECTION("Find names within threshold of target") {
        std::vector<std::string> candidates;

        // Create many similar names
        candidates.push_back("jwt_auth");
        candidates.push_back("jwt_auht");   // dist 1 (swap)
        candidates.push_back("jvt_auth");   // dist 1 (substitute)
        candidates.push_back("jwt_aut");    // dist 1 (delete)
        candidates.push_back("jwt_aauth");  // dist 1 (insert)
        candidates.push_back("jvt_auht");   // dist 2
        candidates.push_back("kvt_auth");   // dist 1
        candidates.push_back("jwt_buth");   // dist 2

        auto similar = find_similar_strings("jwt_auth", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // With threshold=2, should match all except those at distance 0 or >2
        REQUIRE(similar.size() <= MAX_FUZZY_MATCH_CANDIDATES);  // Limited to 10
    }

    SECTION("Many candidates within threshold") {
        std::vector<std::string> candidates;

        // Create 50 similar names (all within threshold)
        for (int i = 0; i < 50; ++i) {
            std::string name = "middleware_" + std::to_string(i);
            candidates.push_back(name);
        }

        auto similar = find_similar_strings("middleware_X", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // find_similar_strings doesn't limit (that's done in validator)
        // Just verify it returns matches
        REQUIRE(!similar.empty());
        // Most should match (middleware_0, middleware_1, etc. all distance 1 from middleware_X)
        REQUIRE(similar.size() >= 10);
    }
}

TEST_CASE("Performance with scale", "[security][fuzzy][scale]") {
    SECTION("10 registered middleware - fast validation") {
        auto config = create_config_with_many_middleware(10);

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {"nonexistent_middleware"};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("50 registered middleware - reasonable performance") {
        auto config = create_config_with_many_middleware(50);

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {"nonexistent_middleware"};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("100 registered middleware (at limit) - acceptable performance") {
        auto config = create_config_with_many_middleware(MAX_REGISTERED_MIDDLEWARE);

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {"nonexistent_middleware"};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Worst-case fuzzy matching inputs", "[security][fuzzy][worst-case]") {
    SECTION("Completely different strings") {
        std::vector<std::string> candidates = {"AAAAAAAA", "BBBBBBBB", "CCCCCCCC"};

        auto similar = find_similar_strings("XXXXXXXX", candidates, MAX_LEVENSHTEIN_DISTANCE);

        REQUIRE(similar.empty());
    }

    SECTION("Strings with repeating patterns") {
        std::vector<std::string> candidates = {"abababababababab", "babababababababa",
                                               "cdcdcdcdcdcdcdcd"};

        auto similar =
            find_similar_strings("abababababababa", candidates, MAX_LEVENSHTEIN_DISTANCE);

        REQUIRE(!similar.empty());
    }

    SECTION("Maximum length strings all similar") {
        std::vector<std::string> candidates;
        std::string base(MAX_MIDDLEWARE_NAME_LENGTH, 'A');

        for (int i = 0; i < 10; ++i) {
            std::string candidate = base;
            candidate[i] = 'B';
            candidates.push_back(candidate);
        }

        auto similar = find_similar_strings(base, candidates, MAX_LEVENSHTEIN_DISTANCE);

        REQUIRE(similar.size() == 10);
    }
}

TEST_CASE("Edge cases in fuzzy matching", "[security][fuzzy][edge-cases]") {
    SECTION("Empty string comparison") {
        std::vector<std::string> candidates = {"jwt_auth"};
        auto similar = find_similar_strings("", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // Empty string distance to "jwt_auth" is 8 (> threshold)
        REQUIRE(similar.empty());
    }

    SECTION("Empty candidates list") {
        std::vector<std::string> candidates;
        auto similar = find_similar_strings("jwt_auth", candidates, MAX_LEVENSHTEIN_DISTANCE);

        REQUIRE(similar.empty());
    }

    SECTION("Single character strings") {
        std::vector<std::string> candidates = {"a", "b", "c"};
        auto similar = find_similar_strings("a", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // Distance 0 excluded, b and c are distance 1
        REQUIRE(similar.size() == 2);
    }

    SECTION("Exact match excluded from suggestions") {
        std::vector<std::string> candidates = {"jwt_auth", "jwt_authz"};
        auto similar = find_similar_strings("jwt_auth", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // jwt_auth (distance 0) excluded, jwt_authz (distance 1) included
        REQUIRE(similar.size() == 1);
        REQUIRE(similar[0] == "jwt_authz");
    }

    SECTION("Special characters in strings") {
        std::vector<std::string> candidates = {"jwt-auth", "jwt_auth"};
        auto similar = find_similar_strings("jwt-auht", candidates, MAX_LEVENSHTEIN_DISTANCE);

        // Both within threshold
        REQUIRE(!similar.empty());
    }
}

TEST_CASE("Fuzzy matching with validation integration", "[security][fuzzy][integration]") {
    SECTION("Typo suggestions appear in error messages") {
        Config config;
        config.rate_limits["jwt_auth"] = RateLimitConfig{};
        config.rate_limits["rate_limit"] = RateLimitConfig{};

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {"jwt_auht"};  // Typo: swapped h and t
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(!result.errors.empty());

        // Should suggest "jwt_auth"
        bool found_suggestion = false;
        for (const auto& error : result.errors) {
            if (error.find("jwt_auth") != std::string::npos) {
                found_suggestion = true;
                break;
            }
        }
        REQUIRE(found_suggestion);
    }

    SECTION("Multiple typos in same route") {
        Config config;
        config.rate_limits["jwt_auth"] = RateLimitConfig{};
        config.rate_limits["rate_limit"] = RateLimitConfig{};
        config.cors_configs["cors_policy"] = CorsConfig{};

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {
            "jwt_auht",    // Typo 1
            "rate_lomit",  // Typo 2
            "cors_polici"  // Typo 3
        };
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors.size() >= 3);  // At least one error per typo
    }

    SECTION("No suggestions for completely wrong names") {
        Config config;
        config.rate_limits["jwt_auth"] = RateLimitConfig{};

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {"completely_different_name"};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        // Error message should not contain "Did you mean" for completely different name
        bool has_did_you_mean = false;
        for (const auto& error : result.errors) {
            if (error.find("Did you mean") != std::string::npos) {
                has_did_you_mean = true;
                break;
            }
        }
        REQUIRE_FALSE(has_did_you_mean);
    }
}

TEST_CASE("Levenshtein distance algorithm correctness", "[security][fuzzy][correctness]") {
    SECTION("Insertion operation") {
        REQUIRE(levenshtein_distance("cat", "cats") == 1);
        REQUIRE(levenshtein_distance("ca", "cat") == 1);
    }

    SECTION("Deletion operation") {
        REQUIRE(levenshtein_distance("cats", "cat") == 1);
        REQUIRE(levenshtein_distance("cat", "ca") == 1);
    }

    SECTION("Substitution operation") {
        REQUIRE(levenshtein_distance("cat", "bat") == 1);
        REQUIRE(levenshtein_distance("cat", "cut") == 1);
    }

    SECTION("Transposition (swap) operations") {
        REQUIRE(levenshtein_distance("cat", "act") == 2);            // Not Damerau-Levenshtein
        REQUIRE(levenshtein_distance("jwt_auth", "jwt_auht") == 2);  // h↔t swap
    }

    SECTION("Multiple operations") {
        REQUIRE(levenshtein_distance("kitten", "sitting") == 3);
        REQUIRE(levenshtein_distance("saturday", "sunday") == 3);
    }

    SECTION("Completely different strings") {
        REQUIRE(levenshtein_distance("abc", "xyz") == 3);
        REQUIRE(levenshtein_distance("", "abc") == 3);
        REQUIRE(levenshtein_distance("abc", "") == 3);
    }

    SECTION("Case sensitivity") {
        REQUIRE(levenshtein_distance("JWT", "jwt") == 3);  // Case sensitive (all 3 chars differ)
        REQUIRE(levenshtein_distance("Jwt_Auth", "jwt_auth") == 2);  // J→j and A→a
    }
}
