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

#include <catch2/catch_test_macros.hpp>

#include "gateway/jwt_authz_middleware.hpp"
#include "http/http.hpp"

using namespace titan::gateway;
using namespace titan::http;

// ============================================================================
// Helper Functions
// ============================================================================

// Helper to manage request/response lifecycle
struct TestContext {
    Request req;
    Response resp;
    RequestContext ctx;

    TestContext() {
        ctx.request = &req;
        ctx.response = &resp;
        ctx.request->method = Method::GET;
        ctx.request->path = "/api/test";
        ctx.client_ip = "127.0.0.1";
        ctx.correlation_id = "test-123";

        // Initialize route_match with defaults
        ctx.route_match.auth_required = false;
    }
};

static RequestContext& create_test_context(TestContext& tc) {
    return tc.ctx;
}

// ============================================================================
// Scope Matching Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware scope matching", "[jwt][authz][scope]") {
    JwtAuthzMiddleware::Config config;
    config.require_all_scopes = false;  // OR logic
    JwtAuthzMiddleware middleware(config);

    SECTION("No authorization required - allow all") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = false;
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("Authorization not required - allow all") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = false;
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User has required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "read:users write:posts");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "read:posts");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has one of multiple required scopes (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users", "write:users"};
        ctx.set_metadata("jwt_scope", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing all required scopes (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users", "write:users"};
        ctx.set_metadata("jwt_scope", "read:posts");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

TEST_CASE("JwtAuthzMiddleware scope matching - AND logic", "[jwt][authz][scope]") {
    JwtAuthzMiddleware::Config config;
    config.require_all_scopes = true;  // AND logic
    JwtAuthzMiddleware middleware(config);

    SECTION("User has all required scopes") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users", "write:users"};
        ctx.set_metadata("jwt_scope", "read:users write:users delete:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing one required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users", "write:users"};
        ctx.set_metadata("jwt_scope", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Role Matching Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware role matching", "[jwt][authz][role]") {
    JwtAuthzMiddleware::Config config;
    config.require_all_roles = false;  // OR logic
    JwtAuthzMiddleware middleware(config);

    SECTION("User has required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin"};
        ctx.set_metadata("jwt_roles", "admin moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin"};
        ctx.set_metadata("jwt_roles", "user");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has one of multiple required roles (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin", "moderator"};
        ctx.set_metadata("jwt_roles", "moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing all required roles (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin", "moderator"};
        ctx.set_metadata("jwt_roles", "user");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

TEST_CASE("JwtAuthzMiddleware role matching - AND logic", "[jwt][authz][role]") {
    JwtAuthzMiddleware::Config config;
    config.require_all_roles = true;  // AND logic
    JwtAuthzMiddleware middleware(config);

    SECTION("User has all required roles") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin", "moderator"};
        ctx.set_metadata("jwt_roles", "admin moderator user");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing one required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_roles = {"admin", "moderator"};
        ctx.set_metadata("jwt_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Combined Scope and Role Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware combined scope and role", "[jwt][authz][combined]") {
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config);

    SECTION("User has required scope and role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.route_match.required_roles = {"admin"};
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("jwt_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User has scope but missing role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.route_match.required_roles = {"admin"};
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("jwt_roles", "user");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has role but missing scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.route_match.required_roles = {"admin"};
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("jwt_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("JwtAuthzMiddleware edge cases", "[jwt][authz][edge]") {
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config);

    SECTION("No JWT claims present") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        // No jwt_scope or jwt_roles set

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Empty scope string") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Whitespace-only scope string") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "   ");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Multiple spaces between scopes") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"write:users"};
        ctx.set_metadata("jwt_scope", "read:users    write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("Disabled middleware") {
        JwtAuthzMiddleware::Config disabled_config;
        disabled_config.enabled = false;
        JwtAuthzMiddleware disabled_middleware(disabled_config);

        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        // No jwt_scope set (should fail if middleware was enabled)

        auto result = disabled_middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);  // Middleware disabled, always passes
    }
}

// ============================================================================
// Response Format Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware 403 response format", "[jwt][authz][response]") {
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config);

    SECTION("403 response includes JSON error body") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "read:posts");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);

        // Check response body
        std::string body(reinterpret_cast<const char*>(ctx.response->body.data()),
                         ctx.response->body.size());
        REQUIRE(body.find("forbidden") != std::string::npos);
        REQUIRE(body.find("Insufficient permissions") != std::string::npos);

        // Check Content-Type header
        auto content_type = ctx.response->get_header("Content-Type");
        REQUIRE(content_type == "application/json");
    }

    SECTION("Error is set in context") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.route_match.auth_required = true;
        ctx.route_match.required_scopes = {"read:users"};
        ctx.set_metadata("jwt_scope", "read:posts");

        middleware.process_request(ctx);

        REQUIRE(!ctx.error_message.empty());
        REQUIRE(ctx.error_message.find("Authorization failed") != std::string::npos);
    }
}
