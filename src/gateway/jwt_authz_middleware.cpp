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

// Titan JWT Authorization Middleware - Implementation

#include "jwt_authz_middleware.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

#include "../core/logging.hpp"

namespace titan::gateway {

JwtAuthzMiddleware::JwtAuthzMiddleware(Config config, std::shared_ptr<Router> router)
    : config_(std::move(config)), router_(std::move(router)) {
    assert(router_ && "Router must not be null");
}

MiddlewareResult JwtAuthzMiddleware::process_request(RequestContext& ctx) {
    if (!config_.enabled || !router_) {
        return MiddlewareResult::Continue;
    }

    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // STEP 1: Get route configuration to check authorization requirements
    // Note: Route matching is done by router earlier, we use matched_route if available
    // For now, we check if route requires auth via matched_route metadata

    // Check if route requires authentication
    std::string_view auth_required_str = ctx.get_metadata("route_auth_required");
    if (auth_required_str.empty() || auth_required_str == "false") {
        // No authorization required for this route
        return MiddlewareResult::Continue;
    }

    // STEP 2: Get JWT claims from context (set by JwtAuthMiddleware)
    std::string_view jwt_scope = ctx.get_metadata("jwt_scope");
    std::string_view jwt_roles = ctx.get_metadata("jwt_roles");  // Custom claim

    // If no JWT claims present, user is not authenticated
    // (JwtAuthMiddleware should have already rejected if JWT was required)
    if (jwt_scope.empty() && jwt_roles.empty()) {
        return send_403(ctx, "Missing required claims");
    }

    // STEP 3: Check required scopes
    std::string_view required_scopes_str = ctx.get_metadata("route_required_scopes");
    if (!required_scopes_str.empty()) {
        // Parse required scopes (comma-separated in metadata)
        std::vector<std::string> required_scopes;
        std::string scopes_str{required_scopes_str};  // Convert to string
        std::istringstream ss{scopes_str};
        std::string scope;
        while (std::getline(ss, scope, ',')) {
            required_scopes.push_back(scope);
        }

        if (!has_required_scopes(jwt_scope, required_scopes)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");
            LOG_WARNING(logger, "Authorization failed: missing scopes, user_scopes={}, "
                               "required_scopes={}, client_ip={}, correlation_id={}",
                        jwt_scope, required_scopes_str, ctx.client_ip, ctx.correlation_id);

            return send_403(ctx, "Insufficient permissions");
        }
    }

    // STEP 4: Check required roles
    std::string_view required_roles_str = ctx.get_metadata("route_required_roles");
    if (!required_roles_str.empty()) {
        // Parse required roles (comma-separated in metadata)
        std::vector<std::string> required_roles;
        std::string roles_str{required_roles_str};  // Convert to string
        std::istringstream ss{roles_str};
        std::string role;
        while (std::getline(ss, role, ',')) {
            required_roles.push_back(role);
        }

        if (!has_required_roles(jwt_roles, required_roles)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");
            LOG_WARNING(logger, "Authorization failed: missing roles, user_roles={}, "
                               "required_roles={}, client_ip={}, correlation_id={}",
                        jwt_roles, required_roles_str, ctx.client_ip, ctx.correlation_id);

            return send_403(ctx, "Insufficient permissions");
        }
    }

    // Authorization successful
    return MiddlewareResult::Continue;
}

MiddlewareResult JwtAuthzMiddleware::send_403(RequestContext& ctx, std::string_view error) const {
    if (ctx.response) {
        ctx.response->status = http::StatusCode::Forbidden;

        // Set generic error body (don't leak authorization details)
        static constexpr const char* error_body =
            R"({"error":"forbidden","message":"Insufficient permissions"})";
        ctx.response->body =
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(error_body),
                                     std::char_traits<char>::length(error_body));
        ctx.response->add_header("Content-Type", "application/json");
    }

    ctx.set_error("Authorization failed: " + std::string(error));
    return MiddlewareResult::Stop;
}

bool JwtAuthzMiddleware::has_required_scopes(std::string_view user_scopes,
                                               const std::vector<std::string>& required_scopes) const {
    if (required_scopes.empty()) {
        return true;  // No scopes required
    }

    // Parse user scopes (space-separated, OAuth 2.0 standard)
    auto user_scope_list = parse_space_separated(user_scopes);

    if (config_.require_all_scopes) {
        // AND logic: user must have ALL required scopes
        for (const auto& required : required_scopes) {
            bool found = std::find(user_scope_list.begin(), user_scope_list.end(), required) !=
                         user_scope_list.end();
            if (!found) {
                return false;
            }
        }
        return true;
    } else {
        // OR logic: user must have AT LEAST ONE required scope
        for (const auto& required : required_scopes) {
            bool found = std::find(user_scope_list.begin(), user_scope_list.end(), required) !=
                         user_scope_list.end();
            if (found) {
                return true;
            }
        }
        return false;
    }
}

bool JwtAuthzMiddleware::has_required_roles(std::string_view user_roles,
                                              const std::vector<std::string>& required_roles) const {
    if (required_roles.empty()) {
        return true;  // No roles required
    }

    // Parse user roles (space-separated or JSON array - for now space-separated)
    auto user_role_list = parse_space_separated(user_roles);

    if (config_.require_all_roles) {
        // AND logic: user must have ALL required roles
        for (const auto& required : required_roles) {
            bool found = std::find(user_role_list.begin(), user_role_list.end(), required) !=
                         user_role_list.end();
            if (!found) {
                return false;
            }
        }
        return true;
    } else {
        // OR logic: user must have AT LEAST ONE required role
        for (const auto& required : required_roles) {
            bool found = std::find(user_role_list.begin(), user_role_list.end(), required) !=
                         user_role_list.end();
            if (found) {
                return true;
            }
        }
        return false;
    }
}

std::vector<std::string> JwtAuthzMiddleware::parse_space_separated(std::string_view input) const {
    std::vector<std::string> result;

    if (input.empty()) {
        return result;
    }

    std::string input_str{input};  // Convert to string
    std::istringstream ss{input_str};
    std::string token;
    while (ss >> token) {
        if (!token.empty()) {
            result.push_back(token);
        }
    }

    return result;
}

}  // namespace titan::gateway
