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

// Titan Pipeline - Implementation

#include "pipeline.hpp"

#include <cassert>
#include <iostream>

#include "../core/logging.hpp"

namespace titan::gateway {

// LoggingMiddleware implementation (Response phase - logs with timing)

MiddlewareResult LoggingMiddleware::process_response(ResponseContext& ctx) {
    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Logging disabled for performance in MVP
    // TODO: Implement lock-free async logging with background thread for production
    // Calculate duration if needed for metrics:
    // auto now = std::chrono::steady_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //     now - ctx.start_time);

    return MiddlewareResult::Continue;
}

// CorsMiddleware implementation (Request phase - adds CORS headers)

MiddlewareResult CorsMiddleware::process_request(RequestContext& ctx) {
    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Add CORS headers using hybrid storage (add_middleware_header copies strings - safe!)
    if (!config_.allowed_origins.empty()) {
        ctx.response->add_middleware_header("Access-Control-Allow-Origin",
                                            config_.allowed_origins[0]);
    }

    if (!config_.allowed_methods.empty()) {
        std::string methods;
        for (size_t i = 0; i < config_.allowed_methods.size(); ++i) {
            if (i > 0)
                methods += ", ";
            methods += config_.allowed_methods[i];
        }
        ctx.response->add_middleware_header("Access-Control-Allow-Methods", methods);
    }

    if (!config_.allowed_headers.empty()) {
        std::string headers;
        for (size_t i = 0; i < config_.allowed_headers.size(); ++i) {
            if (i > 0)
                headers += ", ";
            headers += config_.allowed_headers[i];
        }
        ctx.response->add_middleware_header("Access-Control-Allow-Headers", headers);
    }

    if (config_.allow_credentials) {
        ctx.response->add_middleware_header("Access-Control-Allow-Credentials", "true");
    }

    std::string max_age = std::to_string(config_.max_age);
    ctx.response->add_middleware_header("Access-Control-Max-Age", max_age);

    // Handle OPTIONS preflight
    if (ctx.request->method == http::Method::OPTIONS) {
        ctx.response->status = http::StatusCode::NoContent;
        return MiddlewareResult::Stop;
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult CorsMiddleware::process_websocket_upgrade(RequestContext& ctx) {
    // CRITICAL: Validate Origin header to prevent CSWSH attacks
    // (Cross-Site WebSocket Hijacking)

    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // If CORS is disabled, skip Origin validation
    if (!config_.enabled) {
        return MiddlewareResult::Continue;
    }

    // Extract Origin header from request
    std::string_view origin;
    for (const auto& [name, value] : ctx.request->headers) {
        if (name == "Origin") {
            origin = value;
            break;
        }
    }

    // Validate Origin against allowed_origins
    bool origin_allowed = false;

    for (const auto& allowed_origin : config_.allowed_origins) {
        // Check for wildcard (DANGEROUS for WebSocket - should only allow in dev)
        if (allowed_origin == "*") {
            origin_allowed = true;
            break;
        }

        // Exact match (case-sensitive per RFC 6454)
        if (origin == allowed_origin) {
            origin_allowed = true;
            break;
        }
    }

    if (!origin_allowed) {
        // Origin not in allowed list - REJECT upgrade (CSWSH prevention)
        ctx.response->status = http::StatusCode::Forbidden;
        static constexpr const char* error_msg = "Origin not allowed for WebSocket upgrade";
        ctx.response->body = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(error_msg),
                                                      std::char_traits<char>::length(error_msg));
        ctx.set_error(error_msg);

        // Log security violation
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_WARNING(logger,
                    "WebSocket upgrade blocked - Origin '{}' not in allowed_origins, client_ip={}",
                    origin.empty() ? "(missing)" : std::string(origin), ctx.client_ip);

        return MiddlewareResult::Stop;  // Block upgrade
    }

    // Origin is valid - allow upgrade to continue
    return MiddlewareResult::Continue;
}

// RateLimitMiddleware implementation (Request phase - checks rate limits)

RateLimitMiddleware::RateLimitMiddleware()
    : config_(),
      limiter_(std::make_unique<ThreadLocalRateLimiter>(config_.burst_size,
                                                        config_.requests_per_second)) {}

RateLimitMiddleware::RateLimitMiddleware(Config config)
    : config_(std::move(config)),
      limiter_(std::make_unique<ThreadLocalRateLimiter>(config_.burst_size,
                                                        config_.requests_per_second)) {}

MiddlewareResult RateLimitMiddleware::process_request(RequestContext& ctx) {
    // Use client IP as the rate limit key
    std::string_view key = ctx.client_ip;
    if (key.empty()) {
        // No client IP, allow request (or could deny)
        return MiddlewareResult::Continue;
    }

    // Check rate limit
    if (!limiter_->allow(key)) {
        // Rate limit exceeded
        if (ctx.response) {
            ctx.response->status = http::StatusCode::TooManyRequests;
        }
        ctx.set_error("Rate limit exceeded");

        // Log rate limit violation (branchless in release builds)
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_ERROR(logger, "Rate limit exceeded: client_ip={}, correlation_id={}", ctx.client_ip,
                  ctx.correlation_id);

        return MiddlewareResult::Stop;
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult RateLimitMiddleware::process_websocket_upgrade(RequestContext& ctx) {
    // Rate limit WebSocket connection attempts (per client IP)
    // NOTE: This limits upgrade requests, not individual messages

    std::string_view key = ctx.client_ip;
    if (key.empty()) {
        // No client IP, allow upgrade
        return MiddlewareResult::Continue;
    }

    // Check rate limit using same limiter as HTTP requests
    if (!limiter_->allow(key)) {
        // Rate limit exceeded - block WebSocket upgrade
        if (ctx.response) {
            ctx.response->status = http::StatusCode::TooManyRequests;
            ctx.response->add_middleware_header("Retry-After", "60");
        }
        ctx.set_error("WebSocket connection rate limit exceeded");

        // Log rate limit violation
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_ERROR(logger, "WebSocket upgrade rate limit exceeded: client_ip={}, correlation_id={}",
                  ctx.client_ip, ctx.correlation_id);

        return MiddlewareResult::Stop;
    }

    return MiddlewareResult::Continue;
}

// ProxyMiddleware implementation

MiddlewareResult ProxyMiddleware::process_request(RequestContext& ctx) {
    // Check if this route has an upstream configured
    if (ctx.route_match.upstream_name.empty()) {
        // Not a proxy route - continue to next middleware
        return MiddlewareResult::Continue;
    }

    // Get upstream from manager
    auto* upstream = upstream_manager_->get_upstream(ctx.route_match.upstream_name);
    if (!upstream) {
        // Upstream not found - return 502
        ctx.set_error("Upstream not found: " + std::string(ctx.route_match.upstream_name));
        if (ctx.response) {
            ctx.response->status = http::StatusCode::BadGateway;
        }

        // Log upstream not found error (branchless in release builds)
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_ERROR(logger, "Upstream not found: upstream={}, correlation_id={}",
                  ctx.route_match.upstream_name, ctx.correlation_id);

        return MiddlewareResult::Stop;
    }

    // Check if upstream has healthy backends
    if (upstream->healthy_count() == 0) {
        // No healthy backends - return 503
        ctx.set_error("No healthy backends for upstream: " +
                      std::string(ctx.route_match.upstream_name));
        if (ctx.response) {
            ctx.response->status = http::StatusCode::ServiceUnavailable;
        }

        // Log no healthy backends error (branchless in release builds)
        auto* logger = logging::get_current_logger();
        assert(logger && "Logger must be initialized");
        LOG_ERROR(logger, "No healthy backends: upstream={}, correlation_id={}",
                  ctx.route_match.upstream_name, ctx.correlation_id);

        return MiddlewareResult::Stop;
    }

    // Store upstream pointer for Server to use
    ctx.upstream = upstream;

    // Optional: Add custom headers for backend
    // ctx.request->add_header("X-Forwarded-For", ctx.client_ip);
    // ctx.request->add_header("X-Real-IP", ctx.client_ip);

    return MiddlewareResult::Continue;
}

MiddlewareResult ProxyMiddleware::process_response(ResponseContext& ctx) {
    // Record circuit breaker feedback
    if (ctx.backend && ctx.backend->circuit_breaker) {
        if (ctx.backend_error) {
            // Backend timeout or connection error
            ctx.backend->circuit_breaker->record_failure();
        } else if (ctx.response) {
            // Got response from backend
            if (ctx.response->status >= http::StatusCode::InternalServerError) {
                // 5xx error - backend failure
                ctx.backend->circuit_breaker->record_failure();
            } else if (ctx.response->status < http::StatusCode::BadRequest) {
                // 2xx or 3xx - success
                ctx.backend->circuit_breaker->record_success();
            }
            // 4xx errors are client errors, don't count as backend failure
        }
    }

    // Add proxy identification header using hybrid storage
    if (ctx.response) {
        // add_middleware_header() copies strings to owned storage - safe from
        // stack-use-after-return
        ctx.response->add_middleware_header("X-Proxy", "Titan");

        // Add timing information
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - ctx.start_time);

        // Temporary string is copied by add_middleware_header() - completely safe!
        std::string timing_value = std::to_string(duration.count()) + "ms";
        ctx.response->add_middleware_header("X-Response-Time", timing_value);
    }

    return MiddlewareResult::Continue;
}

// Pipeline implementation

void Pipeline::use(std::unique_ptr<Middleware> middleware) {
    middleware_.push_back(std::move(middleware));
}

void Pipeline::use(MiddlewareFunc func, std::string_view name) {
    middleware_.push_back(std::make_unique<FunctionMiddleware>(std::move(func), std::string(name)));
}

void Pipeline::register_named_middleware(std::string name, std::unique_ptr<Middleware> middleware) {
    named_middleware_[std::move(name)] = std::move(middleware);
}

Middleware* Pipeline::get_named_middleware(const std::string& name) const {
    auto it = named_middleware_.find(name);
    return (it != named_middleware_.end()) ? it->second.get() : nullptr;
}

MiddlewareResult Pipeline::execute_request(RequestContext& ctx) {
    // Collect types of per-route middleware for override detection (REPLACEMENT model)
    titan::core::fast_set<std::string_view> route_middleware_types;
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware) {
            auto type = middleware->type();
            if (!type.empty()) {
                route_middleware_types.insert(type);
            }
        }
    }

    // Execute global middleware (skip if route provides same type - REPLACEMENT)
    for (auto& middleware : middleware_) {
        auto type = middleware->type();
        if (!type.empty() && route_middleware_types.contains(type)) {
            // Skip: route middleware will override this global middleware
            continue;
        }

        MiddlewareResult result = middleware->process_request(ctx);

        if (result == MiddlewareResult::Stop) {
            return MiddlewareResult::Stop;
        }

        if (result == MiddlewareResult::Error || ctx.has_error) {
            return MiddlewareResult::Error;
        }
    }

    // Execute per-route middleware
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware) {
            MiddlewareResult result = middleware->process_request(ctx);

            if (result == MiddlewareResult::Stop) {
                return MiddlewareResult::Stop;
            }

            if (result == MiddlewareResult::Error || ctx.has_error) {
                return MiddlewareResult::Error;
            }
        }
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult Pipeline::execute_response(ResponseContext& ctx) {
    // Collect types of per-route middleware for override detection (REPLACEMENT model)
    titan::core::fast_set<std::string_view> route_middleware_types;
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware) {
            auto type = middleware->type();
            if (!type.empty()) {
                route_middleware_types.insert(type);
            }
        }
    }

    // Execute global middleware (skip if route provides same type - REPLACEMENT)
    for (auto& middleware : middleware_) {
        auto type = middleware->type();
        if (!type.empty() && route_middleware_types.contains(type)) {
            // Skip: route middleware will override this global middleware
            continue;
        }

        MiddlewareResult result = middleware->process_response(ctx);

        if (result == MiddlewareResult::Stop) {
            return MiddlewareResult::Stop;
        }

        if (result == MiddlewareResult::Error) {
            return MiddlewareResult::Error;
        }
    }

    // Execute per-route middleware
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware) {
            MiddlewareResult result = middleware->process_response(ctx);

            if (result == MiddlewareResult::Stop) {
                return MiddlewareResult::Stop;
            }

            if (result == MiddlewareResult::Error) {
                return MiddlewareResult::Error;
            }
        }
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult Pipeline::execute_websocket_upgrade(RequestContext& ctx) {
    // Execute WebSocket-compatible middleware (before 101 Switching Protocols)
    // Similar to execute_request but only runs middleware that support WebSocket

    // Collect types of per-route middleware for override detection
    titan::core::fast_set<std::string_view> route_middleware_types;
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware && middleware->applies_to_websocket()) {
            auto type = middleware->type();
            if (!type.empty()) {
                route_middleware_types.insert(type);
            }
        }
    }

    // Execute global middleware (skip if route provides same type)
    for (auto& middleware : middleware_) {
        if (!middleware->applies_to_websocket()) {
            continue;  // Skip middleware that doesn't support WebSocket
        }

        auto type = middleware->type();
        if (!type.empty() && route_middleware_types.contains(type)) {
            continue;  // Route middleware overrides global
        }

        MiddlewareResult result = middleware->process_websocket_upgrade(ctx);

        if (result == MiddlewareResult::Stop || result == MiddlewareResult::Error) {
            return result;
        }
    }

    // Execute per-route middleware
    for (const auto& middleware_name : ctx.route_match.middleware) {
        Middleware* middleware = get_named_middleware(middleware_name);
        if (middleware && middleware->applies_to_websocket()) {
            MiddlewareResult result = middleware->process_websocket_upgrade(ctx);

            if (result == MiddlewareResult::Stop || result == MiddlewareResult::Error) {
                return result;
            }
        }
    }

    return MiddlewareResult::Continue;
}

}  // namespace titan::gateway
