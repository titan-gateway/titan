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
        // add_middleware_header() copies strings to owned storage - safe from stack-use-after-return
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

MiddlewareResult Pipeline::execute_request(RequestContext& ctx) {
    for (auto& middleware : middleware_) {
        MiddlewareResult result = middleware->process_request(ctx);

        if (result == MiddlewareResult::Stop) {
            return MiddlewareResult::Stop;
        }

        if (result == MiddlewareResult::Error || ctx.has_error) {
            return MiddlewareResult::Error;
        }
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult Pipeline::execute_response(ResponseContext& ctx) {
    for (auto& middleware : middleware_) {
        MiddlewareResult result = middleware->process_response(ctx);

        if (result == MiddlewareResult::Stop) {
            return MiddlewareResult::Stop;
        }

        if (result == MiddlewareResult::Error) {
            return MiddlewareResult::Error;
        }
    }

    return MiddlewareResult::Continue;
}

}  // namespace titan::gateway
