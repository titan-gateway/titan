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

JwtAuthzMiddleware::JwtAuthzMiddleware(Config config) : config_(std::move(config)) {}

MiddlewareResult JwtAuthzMiddleware::process_request(RequestContext& ctx) {
    if (!config_.enabled) {
        return MiddlewareResult::Continue;
    }

    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // STEP 1: Check if route requires authorization
    // Route matching populates ctx.route_match with authorization requirements
    if (!ctx.route_match.auth_required) {
        return MiddlewareResult::Continue;
    }

    // If no scopes or roles required, nothing to authorize
    if (ctx.route_match.required_scopes.empty() && ctx.route_match.required_roles.empty()) {
        return MiddlewareResult::Continue;
    }

    // STEP 2: Get JWT claims from context (set by JwtAuthMiddleware)
    std::string_view jwt_scope = ctx.get_metadata("jwt_scope");
    std::string_view jwt_roles = ctx.get_metadata("jwt_roles");

    // If no JWT claims present, user is not authenticated
    // (JwtAuthMiddleware should have already rejected if JWT was required)
    if (jwt_scope.empty() && jwt_roles.empty()) {
        return send_403(ctx, "Missing required claims");
    }

    // STEP 3: Check required scopes
    if (!ctx.route_match.required_scopes.empty()) {
        if (!has_required_scopes(jwt_scope, ctx.route_match.required_scopes)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");

            // Build scope string for logging
            std::string required_scopes_str;
            for (size_t i = 0; i < ctx.route_match.required_scopes.size(); ++i) {
                if (i > 0)
                    required_scopes_str += ",";
                required_scopes_str += ctx.route_match.required_scopes[i];
            }

            // Security: Sanitize user-controlled scopes before logging
            std::string safe_scopes = sanitize_for_logging(jwt_scope);

            LOG_WARNING(logger,
                        "Authorization failed: missing scopes, user_scopes={}, "
                        "required_scopes={}, client_ip={}, correlation_id={}",
                        safe_scopes, required_scopes_str, ctx.client_ip, ctx.correlation_id);

            return send_403(ctx, "Insufficient permissions");
        }
    }

    // STEP 4: Check required roles
    if (!ctx.route_match.required_roles.empty()) {
        if (!has_required_roles(jwt_roles, ctx.route_match.required_roles)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");

            // Build role string for logging
            std::string required_roles_str;
            for (size_t i = 0; i < ctx.route_match.required_roles.size(); ++i) {
                if (i > 0)
                    required_roles_str += ",";
                required_roles_str += ctx.route_match.required_roles[i];
            }

            // Security: Sanitize user-controlled roles before logging
            std::string safe_roles = sanitize_for_logging(jwt_roles);

            LOG_WARNING(logger,
                        "Authorization failed: missing roles, user_roles={}, "
                        "required_roles={}, client_ip={}, correlation_id={}",
                        safe_roles, required_roles_str, ctx.client_ip, ctx.correlation_id);

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
        ctx.response->body = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(error_body),
                                                      std::char_traits<char>::length(error_body));
        ctx.response->add_middleware_header("Content-Type", "application/json");
    }

    ctx.set_error("Authorization failed: " + std::string(error));
    return MiddlewareResult::Stop;
}

bool JwtAuthzMiddleware::has_required_scopes(
    std::string_view user_scopes, const std::vector<std::string>& required_scopes) const {
    if (required_scopes.empty()) {
        return true;  // No scopes required
    }

    // Parse user scopes into hash set for O(1) lookup (OAuth 2.0 standard: space-separated)
    // Complexity: O(n) where n = user scope count
    auto user_scope_set = parse_space_separated_set(user_scopes);

    if (config_.require_all_scopes) {
        // AND logic: user must have ALL required scopes
        // Complexity: O(m) where m = required scope count
        // Total: O(n + m) instead of O(n × m)
        for (const auto& required : required_scopes) {
            if (user_scope_set.find(required) == user_scope_set.end()) {
                return false;
            }
        }
        return true;
    } else {
        // OR logic: user must have AT LEAST ONE required scope
        // Complexity: O(m) in worst case, O(1) best case
        for (const auto& required : required_scopes) {
            if (user_scope_set.find(required) != user_scope_set.end()) {
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

    // Parse user roles into hash set for O(1) lookup (space-separated or JSON array - for now
    // space-separated) Complexity: O(n) where n = user role count
    auto user_role_set = parse_space_separated_set(user_roles);

    if (config_.require_all_roles) {
        // AND logic: user must have ALL required roles
        // Complexity: O(m) where m = required role count
        // Total: O(n + m) instead of O(n × m)
        for (const auto& required : required_roles) {
            if (user_role_set.find(required) == user_role_set.end()) {
                return false;
            }
        }
        return true;
    } else {
        // OR logic: user must have AT LEAST ONE required role
        // Complexity: O(m) in worst case, O(1) best case
        for (const auto& required : required_roles) {
            if (user_role_set.find(required) != user_role_set.end()) {
                return true;
            }
        }
        return false;
    }
}

std::vector<std::string> JwtAuthzMiddleware::parse_space_separated(std::string_view input,
                                                                   size_t max_tokens) const {
    std::vector<std::string> result;

    if (input.empty()) {
        return result;
    }

    std::string input_str{input};  // Convert to string
    std::istringstream ss{input_str};
    std::string token;
    size_t count = 0;

    while (ss >> token) {
        if (!token.empty()) {
            result.push_back(token);
            count++;

            // Security: Limit token count to prevent CPU DoS
            if (count >= max_tokens) {
                // Log warning if truncated (but don't break functionality)
                auto* logger = logging::get_current_logger();
                if (logger) {
                    LOG_WARNING(logger, "Scope/role list truncated at {} tokens (security limit)",
                                max_tokens);
                }
                break;
            }
        }
    }

    return result;
}

std::unordered_set<std::string> JwtAuthzMiddleware::parse_space_separated_set(
    std::string_view input, size_t max_tokens) const {
    std::unordered_set<std::string> result;

    if (input.empty()) {
        return result;
    }

    std::string input_str{input};  // Convert to string
    std::istringstream ss{input_str};
    std::string token;
    size_t count = 0;

    while (ss >> token) {
        if (!token.empty()) {
            result.insert(std::move(token));  // O(1) average insertion
            count++;

            // Security: Limit token count to prevent CPU DoS
            if (count >= max_tokens) {
                // Log warning if truncated (but don't break functionality)
                auto* logger = logging::get_current_logger();
                if (logger) {
                    LOG_WARNING(logger, "Scope/role list truncated at {} tokens (security limit)",
                                max_tokens);
                }
                break;
            }
        }
    }

    return result;
}

std::string JwtAuthzMiddleware::sanitize_for_logging(std::string_view input) const {
    if (input.empty()) {
        return "";
    }

    std::string result;
    result.reserve(std::min(input.size(), MAX_LOG_STRING_LENGTH));

    for (size_t i = 0; i < input.size() && i < MAX_LOG_STRING_LENGTH; ++i) {
        char c = input[i];
        // Escape control characters to prevent log injection
        if (c == '\n') {
            result += "\\n";
        } else if (c == '\r') {
            result += "\\r";
        } else if (c == '\t') {
            result += "\\t";
        } else if (c >= 32 && c < 127) {
            result += c;  // Printable ASCII
        } else {
            // Replace non-printable with placeholder
            result += '?';
        }
    }

    // Truncate with indicator if too long
    if (input.size() > MAX_LOG_STRING_LENGTH) {
        result += "...(truncated)";
    }

    return result;
}

}  // namespace titan::gateway
