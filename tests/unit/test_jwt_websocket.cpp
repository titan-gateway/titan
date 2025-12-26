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

// Titan JWT WebSocket Middleware Unit Tests
// These tests focus on WebSocket-specific token extraction (header vs query param)

#include <catch2/catch_test_macros.hpp>

#include "../../src/core/jwt.hpp"
#include "../../src/gateway/jwt_middleware.hpp"
#include "../../src/http/http.hpp"

using namespace titan::gateway;
using namespace titan::http;
using namespace titan::core;

TEST_CASE("JwtAuthMiddleware - WebSocket upgrade when disabled", "[middleware][websocket][jwt]") {
    // Create validator with default config
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware (DISABLED)
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = false;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    Request req;
    req.method = Method::GET;
    req.path = "/ws";
    // No token

    Response res;
    RouteMatch match;
    match.auth_required = true;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_websocket_upgrade(ctx);

    // Should continue when middleware is disabled
    REQUIRE(result == MiddlewareResult::Continue);
}

TEST_CASE("JwtAuthMiddleware - WebSocket upgrade when auth not required",
          "[middleware][websocket][jwt]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware (ENABLED)
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = true;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    Request req;
    req.method = Method::GET;
    req.path = "/ws";
    // No token

    Response res;
    RouteMatch match;
    match.auth_required = false;  // Public route

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_websocket_upgrade(ctx);

    // Should continue when auth is not required
    REQUIRE(result == MiddlewareResult::Continue);
}

TEST_CASE("JwtAuthMiddleware - WebSocket upgrade with missing token",
          "[middleware][websocket][jwt][security]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware (ENABLED)
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = true;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    Request req;
    req.method = Method::GET;
    req.path = "/ws";
    // No Authorization header, no token in query

    Response res;
    RouteMatch match;
    match.auth_required = true;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;
    ctx.client_ip = "192.168.1.100";

    auto result = middleware.process_websocket_upgrade(ctx);

    // Should reject when token is missing
    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::Unauthorized);
    REQUIRE(ctx.has_error == true);
}

TEST_CASE("JwtAuthMiddleware - WebSocket upgrade with invalid token format in header",
          "[middleware][websocket][jwt][security]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = true;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    Request req;
    req.method = Method::GET;
    req.headers = {{"Authorization", "Bearer invalid.token.format"}};

    Response res;
    RouteMatch match;
    match.auth_required = true;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;
    ctx.client_ip = "192.168.1.100";

    auto result = middleware.process_websocket_upgrade(ctx);

    // Should reject invalid token
    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::Unauthorized);
}

TEST_CASE("JwtAuthMiddleware - WebSocket upgrade with Authorization header without Bearer prefix",
          "[middleware][websocket][jwt][security]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = true;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    Request req;
    req.method = Method::GET;
    req.headers = {{"Authorization", "some.token.here"}};  // Missing "Bearer " prefix

    Response res;
    RouteMatch match;
    match.auth_required = true;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;
    ctx.client_ip = "192.168.1.100";

    auto result = middleware.process_websocket_upgrade(ctx);

    // Should reject - invalid format
    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::Unauthorized);
}

TEST_CASE("JwtAuthMiddleware - WebSocket applies_to_websocket returns true",
          "[middleware][websocket][jwt]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = true;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    // Check that applies_to_websocket returns true
    REQUIRE(middleware.applies_to_websocket() == true);
}

TEST_CASE("JwtAuthMiddleware - WebSocket applies_to_websocket returns false when disabled",
          "[middleware][websocket][jwt]") {
    // Create validator
    JwtValidatorConfig validator_config;
    auto validator = std::make_shared<JwtValidator>(validator_config);

    // Create middleware (DISABLED)
    JwtAuthMiddleware::Config middleware_config;
    middleware_config.enabled = false;
    JwtAuthMiddleware middleware(middleware_config, validator, nullptr);

    // Check that applies_to_websocket returns false when disabled
    REQUIRE(middleware.applies_to_websocket() == false);
}
