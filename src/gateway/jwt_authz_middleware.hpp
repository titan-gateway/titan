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

// Titan JWT Authorization Middleware - Header
// Claims-based authorization (scopes and roles)

#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "../core/jwt.hpp"
#include "pipeline.hpp"

namespace titan::gateway {

// Security limits for authorization (DoS prevention)
constexpr size_t MAX_LOG_STRING_LENGTH = 200;  // Max string length before truncation in logs

/// JWT authorization middleware (Phase 1: Claims-based access control)
/// Must run AFTER JwtAuthMiddleware to have access to validated claims
class JwtAuthzMiddleware : public Middleware {
public:
    struct Config {
        bool enabled = true;
        std::string scope_claim = "scope";   // JWT claim containing scopes
        std::string roles_claim = "roles";   // JWT claim containing roles (optional)
        bool require_all_scopes = false;     // true = AND, false = OR
        bool require_all_roles = false;      // true = AND, false = OR
    };

    explicit JwtAuthzMiddleware(Config config);
    ~JwtAuthzMiddleware() override = default;

    /// Process request phase (authorize based on JWT claims)
    [[nodiscard]] MiddlewareResult process_request(RequestContext& ctx) override;

    /// Get middleware name
    [[nodiscard]] std::string_view name() const override { return "JwtAuthzMiddleware"; }

private:
    /// Send 403 Forbidden response
    [[nodiscard]] MiddlewareResult send_403(RequestContext& ctx, std::string_view error) const;

    /// Check if user has required scopes
    [[nodiscard]] bool has_required_scopes(std::string_view user_scopes,
                                            const std::vector<std::string>& required_scopes) const;

    /// Check if user has required roles
    [[nodiscard]] bool has_required_roles(std::string_view user_roles,
                                           const std::vector<std::string>& required_roles) const;

    /// Parse space-separated scope/role string into set (with count limit)
    [[nodiscard]] std::vector<std::string> parse_space_separated(
        std::string_view input, size_t max_tokens = core::MAX_SCOPE_ROLE_COUNT) const;

    /// Parse space-separated scope/role string into hash set for O(1) lookup (with count limit)
    [[nodiscard]] std::unordered_set<std::string> parse_space_separated_set(
        std::string_view input, size_t max_tokens = core::MAX_SCOPE_ROLE_COUNT) const;

    /// Sanitize string for safe logging (escape control characters, truncate)
    [[nodiscard]] std::string sanitize_for_logging(std::string_view input) const;

    Config config_;
};

}  // namespace titan::gateway
