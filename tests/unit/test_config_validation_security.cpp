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

// Config Validation Security Tests
// Tests for input validation, injection prevention, DoS protection

#include <catch2/catch_test_macros.hpp>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"

using namespace titan::control;

// Helper function to create a test config with a single route
Config create_test_config_with_middleware(const std::vector<std::string>& middleware) {
    Config config;

    RouteConfig route;
    route.path = "/test";
    route.upstream = "backend";
    route.middleware = middleware;
    config.routes.push_back(route);

    BackendConfig backend;
    backend.host = "127.0.0.1";
    backend.port = 8080;

    UpstreamConfig upstream;
    upstream.name = "backend";
    upstream.backends.push_back(backend);
    config.upstreams.push_back(upstream);

    return config;
}

// Helper function to create config with named middleware
Config create_config_with_named_middleware(const std::string& name,
                                           const std::string& type = "rate_limit") {
    Config config;

    if (type == "rate_limit") {
        RateLimitConfig rl_config;
        rl_config.enabled = true;
        rl_config.requests_per_second = 100;
        config.rate_limits[name] = rl_config;
    } else if (type == "cors") {
        CorsConfig cors_config;
        config.cors_configs[name] = cors_config;
    }

    return config;
}

TEST_CASE("Path traversal prevention", "[security][config][validation]") {
    SECTION("Rejects .. path traversal") {
        auto config = create_test_config_with_middleware({"../etc/passwd"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(!result.errors.empty());
        // Security validation should catch path traversal (check any error, not just first)
        bool found_path_traversal = false;
        for (const auto& error : result.errors) {
            if (error.find("Path traversal") != std::string::npos ||
                error.find("Path separators") != std::string::npos ||
                error.find("Invalid character") != std::string::npos) {
                found_path_traversal = true;
                break;
            }
        }
        REQUIRE(found_path_traversal);
    }

    SECTION("Rejects ../../ path traversal") {
        auto config = create_test_config_with_middleware({"../../config/secrets"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(!result.errors.empty());
    }

    SECTION("Rejects ./ path traversal") {
        auto config = create_test_config_with_middleware({"./malicious"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects .\\ path traversal") {
        auto config = create_test_config_with_middleware({".\\malicious"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects absolute paths") {
        auto config = create_test_config_with_middleware({"/etc/passwd"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        // Check for either path separator error or invalid character error
        bool found_error = false;
        for (const auto& error : result.errors) {
            if (error.find("Path separators") != std::string::npos ||
                error.find("Invalid character") != std::string::npos) {
                found_error = true;
                break;
            }
        }
        REQUIRE(found_error);
    }

    SECTION("Rejects Windows paths") {
        auto config = create_test_config_with_middleware({"C:\\Windows\\System32"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Null byte injection prevention", "[security][config][validation]") {
    SECTION("Rejects null byte") {
        std::string malicious = "jwt_auth";
        malicious += '\0';
        malicious += "bypass";

        auto config = create_test_config_with_middleware({malicious});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        // Null byte should be caught (either explicitly or as invalid character)
        bool found_error = false;
        for (const auto& error : result.errors) {
            if (error.find("Null byte") != std::string::npos ||
                error.find("Invalid middleware name") != std::string::npos) {
                found_error = true;
                break;
            }
        }
        REQUIRE(found_error);
    }

    SECTION("Rejects embedded null byte") {
        std::string malicious = "jwt";
        malicious += '\0';
        malicious += "auth";

        auto config = create_test_config_with_middleware({malicious});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("CRLF injection prevention", "[security][config][validation]") {
    SECTION("Rejects carriage return") {
        auto config = create_test_config_with_middleware({"jwt_auth\rmalicious"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        // Check for line break or invalid character error
        bool found_error = false;
        for (const auto& error : result.errors) {
            if (error.find("Line breaks not allowed") != std::string::npos ||
                error.find("Invalid middleware name") != std::string::npos) {
                found_error = true;
                break;
            }
        }
        REQUIRE(found_error);
    }

    SECTION("Rejects line feed") {
        auto config = create_test_config_with_middleware({"jwt_auth\nmalicious"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects CRLF sequence") {
        auto config = create_test_config_with_middleware({"jwt_auth\r\nmalicious: true"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects multiline injection") {
        auto config = create_test_config_with_middleware({"compress\n\nadmin: true"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Length limits enforcement", "[security][config][validation][dos]") {
    SECTION("Rejects excessively long middleware name (100 chars)") {
        std::string long_name(100, 'A');
        auto config = create_test_config_with_middleware({long_name});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("too long") != std::string::npos);
    }

    SECTION("Rejects 1KB middleware name") {
        std::string long_name(1024, 'A');
        auto config = create_test_config_with_middleware({long_name});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects 10KB middleware name") {
        std::string long_name(10000, 'A');
        auto config = create_test_config_with_middleware({long_name});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Accepts maximum allowed length (64 chars)") {
        std::string name(MAX_MIDDLEWARE_NAME_LENGTH, 'A');
        auto config = create_config_with_named_middleware(name);

        RouteConfig route;
        route.path = "/test";
        route.upstream = "backend";
        route.middleware = {name};
        config.routes.push_back(route);

        BackendConfig backend;
        backend.host = "127.0.0.1";
        backend.port = 8080;
        UpstreamConfig upstream;
        upstream.name = "backend";
        upstream.backends.push_back(backend);
        config.upstreams.push_back(upstream);

        auto result = ConfigValidator::validate(config);
        REQUIRE(result.valid);  // Should accept exactly 64 chars
    }

    SECTION("Rejects one char over limit (65 chars)") {
        std::string name(MAX_MIDDLEWARE_NAME_LENGTH + 1, 'A');
        auto config = create_test_config_with_middleware({name});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Character whitelist enforcement", "[security][config][validation]") {
    SECTION("Accepts alphanumeric characters") {
        auto config = create_config_with_named_middleware("jwt_auth_123");
        auto result = ConfigValidator::validate(Config{});  // Empty config is valid

        REQUIRE(result.valid);
    }

    SECTION("Accepts underscores") {
        auto config = create_config_with_named_middleware("jwt_auth_middleware");
        auto result = ConfigValidator::validate(Config{});

        REQUIRE(result.valid);
    }

    SECTION("Accepts hyphens") {
        auto config = create_config_with_named_middleware("jwt-auth-middleware");
        auto result = ConfigValidator::validate(Config{});

        REQUIRE(result.valid);
    }

    SECTION("Rejects special characters - angle brackets") {
        auto config = create_test_config_with_middleware({"<script>"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("Invalid character") != std::string::npos);
    }

    SECTION("Rejects special characters - braces") {
        auto config = create_test_config_with_middleware({"{constructor}"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects special characters - dollar sign") {
        auto config = create_test_config_with_middleware({"$ne"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects special characters - semicolon") {
        auto config = create_test_config_with_middleware({"jwt;DROP TABLE"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects special characters - pipe") {
        auto config = create_test_config_with_middleware({"jwt|malicious"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects special characters - ampersand") {
        auto config = create_test_config_with_middleware({"jwt&&bypass"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects spaces") {
        auto config = create_test_config_with_middleware({"jwt auth"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Empty and whitespace validation", "[security][config][validation]") {
    SECTION("Rejects empty middleware name") {
        auto config = create_test_config_with_middleware({""});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("cannot be empty") != std::string::npos);
    }

    SECTION("Rejects whitespace-only name") {
        auto config = create_test_config_with_middleware({"   "});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);  // Spaces not allowed
    }

    SECTION("Rejects tab character") {
        auto config = create_test_config_with_middleware({"jwt\tauth"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("SQL injection pattern prevention", "[security][config][validation]") {
    SECTION("Rejects SQL comment injection") {
        auto config = create_test_config_with_middleware({"'; DROP TABLE middleware; --"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects SQL union injection") {
        auto config = create_test_config_with_middleware({"' UNION SELECT * FROM users --"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects SQL OR injection") {
        auto config = create_test_config_with_middleware({"admin' OR '1'='1"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Command injection prevention", "[security][config][validation]") {
    SECTION("Rejects backtick command injection") {
        auto config = create_test_config_with_middleware({"`whoami`"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects parentheses (subshell)") {
        auto config = create_test_config_with_middleware({"$(cat /etc/passwd)"});
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Middleware chain length limits", "[security][config][dos]") {
    SECTION("Accepts chain at limit (20 middleware)") {
        std::vector<std::string> chain;
        for (size_t i = 0; i < MAX_MIDDLEWARE_CHAIN_LENGTH; ++i) {
            chain.push_back("middleware_" + std::to_string(i));
        }

        auto config = create_test_config_with_middleware(chain);

        // Add named middleware for all chain items
        for (const auto& name : chain) {
            RateLimitConfig rl;
            rl.enabled = true;
            rl.requests_per_second = 100;
            config.rate_limits[name] = rl;
        }

        auto result = ConfigValidator::validate(config);
        REQUIRE(result.valid);  // Exactly at limit should be OK
    }

    SECTION("Rejects chain over limit (21 middleware)") {
        std::vector<std::string> chain;
        for (size_t i = 0; i < MAX_MIDDLEWARE_CHAIN_LENGTH + 1; ++i) {
            chain.push_back("middleware_" + std::to_string(i));
        }

        auto config = create_test_config_with_middleware(chain);
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("chain too long") != std::string::npos);
    }

    SECTION("Rejects excessive chain (100 middleware)") {
        std::vector<std::string> chain;
        for (size_t i = 0; i < 100; ++i) {
            chain.push_back("middleware_" + std::to_string(i));
        }

        auto config = create_test_config_with_middleware(chain);
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }

    SECTION("Rejects pathological chain (1000 middleware)") {
        std::vector<std::string> chain;
        for (size_t i = 0; i < 1000; ++i) {
            chain.push_back("middleware_" + std::to_string(i));
        }

        auto config = create_test_config_with_middleware(chain);
        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
    }
}

TEST_CASE("Total middleware count limits", "[security][config][dos]") {
    SECTION("Accepts up to limit (100 middleware)") {
        Config config;

        for (size_t i = 0; i < MAX_REGISTERED_MIDDLEWARE; ++i) {
            RateLimitConfig rl;
            rl.enabled = true;
            rl.requests_per_second = 100;
            config.rate_limits["middleware_" + std::to_string(i)] = rl;
        }

        auto result = ConfigValidator::validate(config);
        REQUIRE(result.valid);
    }

    SECTION("Rejects over limit (101 middleware)") {
        Config config;

        for (size_t i = 0; i < MAX_REGISTERED_MIDDLEWARE + 1; ++i) {
            RateLimitConfig rl;
            rl.enabled = true;
            rl.requests_per_second = 100;
            config.rate_limits["middleware_" + std::to_string(i)] = rl;
        }

        auto result = ConfigValidator::validate(config);

        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("Too many registered middleware") != std::string::npos);
    }

    SECTION("Counts across all middleware types") {
        Config config;

        // 30 rate limiters
        for (size_t i = 0; i < 30; ++i) {
            RateLimitConfig rl;
            config.rate_limits["rl_" + std::to_string(i)] = rl;
        }

        // 30 CORS configs
        for (size_t i = 0; i < 30; ++i) {
            CorsConfig cors;
            config.cors_configs["cors_" + std::to_string(i)] = cors;
        }

        // 30 transform configs
        for (size_t i = 0; i < 30; ++i) {
            TransformConfig transform;
            config.transform_configs["transform_" + std::to_string(i)] = transform;
        }

        // 11 compression configs (total 101 > 100)
        for (size_t i = 0; i < 11; ++i) {
            CompressionConfig compression;
            config.compression_configs["comp_" + std::to_string(i)] = compression;
        }

        auto result = ConfigValidator::validate(config);
        REQUIRE_FALSE(result.valid);  // Total = 101 > 100
    }
}

TEST_CASE("Named middleware validation", "[security][config][validation]") {
    SECTION("Validates rate_limit names") {
        Config config;
        config.rate_limits["../../../etc/passwd"] = RateLimitConfig{};

        auto result = ConfigValidator::validate(config);
        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("Invalid rate_limit name") != std::string::npos);
    }

    SECTION("Validates cors_config names") {
        Config config;
        config.cors_configs["malicious<script>"] = CorsConfig{};

        auto result = ConfigValidator::validate(config);
        REQUIRE_FALSE(result.valid);
        REQUIRE(result.errors[0].find("Invalid cors_config name") != std::string::npos);
    }

    SECTION("Validates transform_config names") {
        Config config;
        // Construct string with null byte explicitly
        std::string name_with_null = "jwt";
        name_with_null += '\0';
        name_with_null += "auth";
        config.transform_configs[name_with_null] = TransformConfig{};

        auto result = ConfigValidator::validate(config);
        REQUIRE_FALSE(result.valid);
    }

    SECTION("Validates compression_config names") {
        Config config;
        config.compression_configs["compress\r\nbypass"] = CompressionConfig{};

        auto result = ConfigValidator::validate(config);
        REQUIRE_FALSE(result.valid);
    }
}
