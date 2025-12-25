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

// Titan JWT Authentication Middleware - Implementation

#include "jwt_middleware.hpp"

#include <cassert>

#include "../core/logging.hpp"

namespace titan::gateway {

JwtAuthMiddleware::JwtAuthMiddleware(Config config, std::shared_ptr<core::JwtValidator> validator,
                                     core::RevocationQueue* revocation_queue)
    : config_(std::move(config)),
      validator_(std::move(validator)),
      revocation_queue_(revocation_queue) {
    assert(validator_ && "JwtValidator must not be null");
}

MiddlewareResult JwtAuthMiddleware::process_request(RequestContext& ctx) {
    if (!config_.enabled || !validator_) {
        return MiddlewareResult::Continue;
    }

    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Check if this route requires authentication
    // If auth_required is false, skip JWT validation (allow public access)
    if (!ctx.route_match.auth_required) {
        return MiddlewareResult::Continue;
    }

    // STEP 1: Extract token from Authorization header
    auto auth_header = ctx.request->get_header(config_.header);
    if (auth_header.empty()) {
        return send_401(ctx, "Missing Authorization header");
    }

    // STEP 2: Parse "Bearer <token>" format
    std::string scheme_prefix = config_.scheme + " ";
    if (auth_header.size() <= scheme_prefix.size() ||
        auth_header.substr(0, scheme_prefix.size()) != scheme_prefix) {
        return send_401(ctx, "Invalid Authorization format");
    }

    std::string_view token = auth_header.substr(scheme_prefix.size());
    if (token.empty()) {
        return send_401(ctx, "Empty token");
    }

    // STEP 3: Validate token (uses cache internally)
    auto result = validator_->validate(token);
    if (!result) {
        // Log validation failure (branchless in release builds)
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_WARNING(logger, "JWT validation failed: error={}, client_ip={}, correlation_id={}",
                    result.error, ctx.client_ip, ctx.correlation_id);

        return send_401(ctx, result.error);
    }

    // STEP 4: Check if token has been revoked
    if (config_.revocation_enabled && revocation_queue_) {
        // Sync from global revocation queue (fast path: just atomic load if empty)
        revocation_list_.sync_from_queue(*revocation_queue_);

        // Check if this token's jti is in the blacklist
        if (!result.claims.jti.empty() && revocation_list_.is_revoked(result.claims.jti)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");
            LOG_WARNING(logger, "JWT revoked: jti={}, client_ip={}, correlation_id={}",
                        result.claims.jti, ctx.client_ip, ctx.correlation_id);

            return send_401(ctx, "Token has been revoked");
        }
    }

    // STEP 5: Store claims in context for downstream middleware
    // Use metadata to make claims available to other middleware (rate limiting, logging, etc.)
    if (!result.claims.sub.empty()) {
        ctx.set_metadata("jwt_sub", result.claims.sub);
    }
    if (!result.claims.scope.empty()) {
        ctx.set_metadata("jwt_scope", result.claims.scope);
    }
    if (!result.claims.roles.empty()) {
        ctx.set_metadata("jwt_roles", result.claims.roles);
    }
    if (!result.claims.iss.empty()) {
        ctx.set_metadata("jwt_iss", result.claims.iss);
    }
    if (!result.claims.jti.empty()) {
        ctx.set_metadata("jwt_jti", result.claims.jti);
    }

    // Store full claims JSON for advanced use cases
    if (!result.claims.custom.empty()) {
        ctx.set_metadata("jwt_claims", result.claims.custom.dump());
    }

    // Log successful authentication (debug level only)
    auto* logger = logging::get_current_logger();
    assert(logger && "Logger must be initialized");
    LOG_DEBUG(logger, "JWT validated: sub={}, client_ip={}, correlation_id={}", result.claims.sub,
              ctx.client_ip, ctx.correlation_id);

    return MiddlewareResult::Continue;
}

MiddlewareResult JwtAuthMiddleware::send_401(RequestContext& ctx, std::string_view error) const {
    if (ctx.response) {
        ctx.response->status = http::StatusCode::Unauthorized;

        // Add WWW-Authenticate header (RFC 6750)
        // Store in metadata to ensure string persists beyond function scope
        std::string auth_challenge = config_.scheme + " realm=\"titan\"";
        if (!error.empty()) {
            // Only include error in development mode (don't leak details in production)
            // TODO: Make this configurable via environment variable or config
            // auth_challenge += ", error=\"" + std::string(error) + "\"";
        }
        // Hybrid storage: add_middleware_header() copies to owned strings
        ctx.response->add_middleware_header("WWW-Authenticate", auth_challenge);

        // Set generic error body (don't leak validation details)
        static constexpr const char* error_body =
            R"({"error":"unauthorized","message":"Authentication required"})";
        ctx.response->body = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(error_body),
                                                      std::char_traits<char>::length(error_body));
        ctx.response->add_middleware_header("Content-Type", "application/json");
    }

    ctx.set_error("JWT validation failed: " + std::string(error));
    return MiddlewareResult::Stop;
}

MiddlewareResult JwtAuthMiddleware::process_websocket_upgrade(RequestContext& ctx) {
    // JWT validation for WebSocket upgrades
    // NOTE: Browsers cannot send custom headers in WebSocket constructor,
    // so we support token in query parameter as fallback

    if (!config_.enabled || !validator_) {
        return MiddlewareResult::Continue;
    }

    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Check if this route requires authentication
    if (!ctx.route_match.auth_required) {
        return MiddlewareResult::Continue;
    }

    // STEP 1: Extract token from Authorization header OR query parameter
    std::string_view token;

    // Try Authorization header first (preferred)
    auto auth_header = ctx.request->get_header(config_.header);
    if (!auth_header.empty()) {
        std::string scheme_prefix = config_.scheme + " ";
        if (auth_header.size() > scheme_prefix.size() &&
            auth_header.substr(0, scheme_prefix.size()) == scheme_prefix) {
            token = auth_header.substr(scheme_prefix.size());
        }
    }

    // Fallback: Try query parameter ?token=xxx (for browser WebSocket clients)
    if (token.empty()) {
        // Extract token from query string
        auto query_pos = ctx.request->path.find('?');
        if (query_pos != std::string::npos) {
            std::string_view query = ctx.request->path.substr(query_pos + 1);
            std::string_view token_param = "token=";

            auto token_pos = query.find(token_param);
            if (token_pos != std::string_view::npos) {
                auto token_start = token_pos + token_param.size();
                auto token_end = query.find('&', token_start);
                if (token_end == std::string_view::npos) {
                    token = query.substr(token_start);
                } else {
                    token = query.substr(token_start, token_end - token_start);
                }
            }
        }
    }

    if (token.empty()) {
        return send_401(ctx, "Missing token (Authorization header or ?token= query param)");
    }

    // STEP 2: Validate token (uses cache internally)
    auto result = validator_->validate(token);
    if (!result) {
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_WARNING(logger,
                    "JWT validation failed for WebSocket: error={}, client_ip={}, correlation_id={}",
                    result.error, ctx.client_ip, ctx.correlation_id);

        return send_401(ctx, result.error);
    }

    // STEP 3: Check if token has been revoked
    if (config_.revocation_enabled && revocation_queue_) {
        revocation_list_.sync_from_queue(*revocation_queue_);

        if (!result.claims.jti.empty() && revocation_list_.is_revoked(result.claims.jti)) {
            auto* logger = logging::get_current_logger();
            assert(logger && "Logger must be initialized");
            LOG_WARNING(logger, "JWT revoked for WebSocket: jti={}, client_ip={}, correlation_id={}",
                        result.claims.jti, ctx.client_ip, ctx.correlation_id);

            return send_401(ctx, "Token has been revoked");
        }
    }

    // STEP 4: Store claims in context
    if (!result.claims.sub.empty()) {
        ctx.set_metadata("jwt_sub", result.claims.sub);
    }
    if (!result.claims.scope.empty()) {
        ctx.set_metadata("jwt_scope", result.claims.scope);
    }
    if (!result.claims.roles.empty()) {
        ctx.set_metadata("jwt_roles", result.claims.roles);
    }
    if (!result.claims.iss.empty()) {
        ctx.set_metadata("jwt_iss", result.claims.iss);
    }
    if (!result.claims.jti.empty()) {
        ctx.set_metadata("jwt_jti", result.claims.jti);
    }

    if (!result.claims.custom.empty()) {
        ctx.set_metadata("jwt_claims", result.claims.custom.dump());
    }

    // Log successful authentication
    auto* logger = logging::get_current_logger();
    assert(logger && "Logger must be initialized");
    LOG_DEBUG(logger, "JWT validated for WebSocket: sub={}, client_ip={}, correlation_id={}",
              result.claims.sub, ctx.client_ip, ctx.correlation_id);

    return MiddlewareResult::Continue;
}

}  // namespace titan::gateway
