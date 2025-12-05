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
#include "gateway/router.hpp"
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
    }
};

static RequestContext& create_test_context(TestContext& tc) {
    return tc.ctx;
}

// ============================================================================
// Scope Matching Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware scope matching", "[jwt][authz][scope]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    config.require_all_scopes = false;  // OR logic
    JwtAuthzMiddleware middleware(config, router);

    SECTION("No authorization required - allow all") {
        TestContext tc;
        auto& ctx = tc.ctx;
        // No route_auth_required metadata
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("Authorization not required - allow all") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "false");
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User has required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users write:posts");
        ctx.set_metadata("route_required_scopes", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("route_required_scopes", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has one of multiple required scopes (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("route_required_scopes", "read:users,write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing all required scopes (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("route_required_scopes", "read:users,write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

TEST_CASE("JwtAuthzMiddleware scope matching - AND logic", "[jwt][authz][scope]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    config.require_all_scopes = true;  // AND logic
    JwtAuthzMiddleware middleware(config, router);

    SECTION("User has all required scopes") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users write:users delete:users");
        ctx.set_metadata("route_required_scopes", "read:users,write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing one required scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("route_required_scopes", "read:users,write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Role Matching Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware role matching", "[jwt][authz][role]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    config.require_all_roles = false;  // OR logic
    JwtAuthzMiddleware middleware(config, router);

    SECTION("User has required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "admin moderator");
        ctx.set_metadata("route_required_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "user");
        ctx.set_metadata("route_required_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has one of multiple required roles (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "moderator");
        ctx.set_metadata("route_required_roles", "admin,moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing all required roles (OR logic)") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "user");
        ctx.set_metadata("route_required_roles", "admin,moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

TEST_CASE("JwtAuthzMiddleware role matching - AND logic", "[jwt][authz][role]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    config.require_all_roles = true;  // AND logic
    JwtAuthzMiddleware middleware(config, router);

    SECTION("User has all required roles") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "admin moderator user");
        ctx.set_metadata("route_required_roles", "admin,moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User missing one required role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_roles", "admin");
        ctx.set_metadata("route_required_roles", "admin,moderator");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Combined Scope and Role Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware combined scope and role", "[jwt][authz][combined]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config, router);

    SECTION("User has required scope and role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("jwt_roles", "admin");
        ctx.set_metadata("route_required_scopes", "read:users");
        ctx.set_metadata("route_required_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("User has scope but missing role") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users");
        ctx.set_metadata("jwt_roles", "user");
        ctx.set_metadata("route_required_scopes", "read:users");
        ctx.set_metadata("route_required_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("User has role but missing scope") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("jwt_roles", "admin");
        ctx.set_metadata("route_required_scopes", "read:users");
        ctx.set_metadata("route_required_roles", "admin");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("JwtAuthzMiddleware edge cases", "[jwt][authz][edge]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config, router);

    SECTION("No JWT claims present") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        // No jwt_scope or jwt_roles set
        ctx.set_metadata("route_required_scopes", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Empty scope string") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "");
        ctx.set_metadata("route_required_scopes", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Whitespace-only scope string") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "   ");
        ctx.set_metadata("route_required_scopes", "read:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(ctx.response->status == StatusCode::Forbidden);
    }

    SECTION("Multiple spaces between scopes") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:users    write:users");
        ctx.set_metadata("route_required_scopes", "write:users");

        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("Disabled middleware") {
        JwtAuthzMiddleware::Config disabled_config;
        disabled_config.enabled = false;
        JwtAuthzMiddleware disabled_middleware(disabled_config, router);

        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("route_required_scopes", "read:users");
        // No jwt_scope set (should fail if middleware was enabled)

        auto result = disabled_middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);  // Middleware disabled, always passes
    }
}

// ============================================================================
// Response Format Tests
// ============================================================================

TEST_CASE("JwtAuthzMiddleware 403 response format", "[jwt][authz][response]") {
    auto router = std::make_shared<Router>();
    JwtAuthzMiddleware::Config config;
    JwtAuthzMiddleware middleware(config, router);

    SECTION("403 response includes JSON error body") {
        TestContext tc;
        auto& ctx = tc.ctx;
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("route_required_scopes", "read:users");

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
        ctx.set_metadata("route_auth_required", "true");
        ctx.set_metadata("jwt_scope", "read:posts");
        ctx.set_metadata("route_required_scopes", "read:users");

        middleware.process_request(ctx);

        REQUIRE(!ctx.error_message.empty());
        REQUIRE(ctx.error_message.find("Authorization failed") != std::string::npos);
    }
}
