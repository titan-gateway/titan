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

// Titan JWT Authentication Middleware - Header

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "../core/jwt.hpp"
#include "../core/jwt_revocation.hpp"
#include "pipeline.hpp"

namespace titan::gateway {

/// JWT authentication middleware (Phase 1: Request validation)
class JwtAuthMiddleware : public Middleware {
public:
    struct Config {
        std::string header = "Authorization";  // Header name
        std::string scheme = "Bearer";         // "Bearer <token>"
        bool enabled = true;
        bool revocation_enabled = true;        // Enable token revocation checking
    };

    explicit JwtAuthMiddleware(Config config, std::shared_ptr<core::JwtValidator> validator,
                                core::RevocationQueue* revocation_queue = nullptr);
    ~JwtAuthMiddleware() override = default;

    /// Process request phase (validate JWT token)
    [[nodiscard]] MiddlewareResult process_request(RequestContext& ctx) override;

    /// Get middleware name
    [[nodiscard]] std::string_view name() const override { return "JwtAuthMiddleware"; }

private:
    /// Send 401 Unauthorized response
    [[nodiscard]] MiddlewareResult send_401(RequestContext& ctx, std::string_view error) const;

    Config config_;
    std::shared_ptr<core::JwtValidator> validator_;

    // Token revocation (thread-local blacklist)
    core::RevocationList revocation_list_;
    core::RevocationQueue* revocation_queue_;  // Shared across all workers (nullable)
};

}  // namespace titan::gateway
