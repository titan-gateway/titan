// Titan Pipeline - Implementation

#include "pipeline.hpp"

#include <iostream>

namespace titan::gateway {

// LoggingMiddleware implementation

MiddlewareResult LoggingMiddleware::process(RequestContext& ctx) {
    if (!ctx.request) {
        return MiddlewareResult::Error;
    }

    // Log request
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - ctx.start_time);

    std::cout << "[" << http::to_string(ctx.request->method) << "] "
              << ctx.request->path << " - "
              << duration.count() << "ms"
              << std::endl;

    return MiddlewareResult::Continue;
}

// CorsMiddleware implementation

MiddlewareResult CorsMiddleware::process(RequestContext& ctx) {
    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Add CORS headers
    if (!config_.allowed_origins.empty()) {
        std::string origin = config_.allowed_origins[0];
        ctx.response->add_header("Access-Control-Allow-Origin", origin);
    }

    if (!config_.allowed_methods.empty()) {
        std::string methods;
        for (size_t i = 0; i < config_.allowed_methods.size(); ++i) {
            if (i > 0) methods += ", ";
            methods += config_.allowed_methods[i];
        }
        ctx.response->add_header("Access-Control-Allow-Methods", methods);
    }

    if (!config_.allowed_headers.empty()) {
        std::string headers;
        for (size_t i = 0; i < config_.allowed_headers.size(); ++i) {
            if (i > 0) headers += ", ";
            headers += config_.allowed_headers[i];
        }
        ctx.response->add_header("Access-Control-Allow-Headers", headers);
    }

    if (config_.allow_credentials) {
        ctx.response->add_header("Access-Control-Allow-Credentials", "true");
    }

    ctx.response->add_header("Access-Control-Max-Age", std::to_string(config_.max_age));

    // Handle OPTIONS preflight
    if (ctx.request->method == http::Method::OPTIONS) {
        ctx.response->status = http::StatusCode::NoContent;
        return MiddlewareResult::Stop;
    }

    return MiddlewareResult::Continue;
}

// RateLimitMiddleware implementation

RateLimitMiddleware::RateLimitMiddleware()
    : config_()
    , limiter_(std::make_unique<ThreadLocalRateLimiter>(config_.burst_size, config_.requests_per_second)) {}

RateLimitMiddleware::RateLimitMiddleware(Config config)
    : config_(std::move(config))
    , limiter_(std::make_unique<ThreadLocalRateLimiter>(config_.burst_size, config_.requests_per_second)) {}

MiddlewareResult RateLimitMiddleware::process(RequestContext& ctx) {
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
        return MiddlewareResult::Stop;
    }

    return MiddlewareResult::Continue;
}

// ProxyMiddleware implementation

MiddlewareResult ProxyMiddleware::process(RequestContext& ctx) {
    if (!ctx.request || !ctx.response) {
        return MiddlewareResult::Error;
    }

    // Get upstream from route match
    if (ctx.route_match.handler_id.empty()) {
        ctx.set_error("No route matched");
        ctx.response->status = http::StatusCode::NotFound;
        return MiddlewareResult::Stop;
    }

    // Get upstream from metadata or route
    std::string_view upstream_name = ctx.get_metadata("upstream");
    if (upstream_name.empty()) {
        ctx.set_error("No upstream specified");
        ctx.response->status = http::StatusCode::BadGateway;
        return MiddlewareResult::Stop;
    }

    // Get upstream
    Upstream* upstream = upstream_manager_->get_upstream(upstream_name);
    if (!upstream) {
        ctx.set_error("Upstream not found");
        ctx.response->status = http::StatusCode::BadGateway;
        return MiddlewareResult::Stop;
    }

    ctx.upstream = upstream;

    // Get connection
    Connection* conn = upstream->get_connection(ctx.client_ip);
    if (!conn) {
        ctx.set_error("No backend available");
        ctx.response->status = http::StatusCode::ServiceUnavailable;
        return MiddlewareResult::Stop;
    }

    ctx.connection = conn;

    // TODO: Actually proxy the request (requires I/O)
    // For now, just return success
    ctx.response->status = http::StatusCode::OK;
    ctx.response->set_content_type("application/json");

    // Release connection
    upstream->release_connection(conn);
    ctx.connection = nullptr;

    return MiddlewareResult::Continue;
}

// Pipeline implementation

void Pipeline::use(std::unique_ptr<Middleware> middleware) {
    middleware_.push_back(std::move(middleware));
}

void Pipeline::use(MiddlewareFunc func, std::string_view name) {
    middleware_.push_back(
        std::make_unique<FunctionMiddleware>(std::move(func), std::string(name)));
}

MiddlewareResult Pipeline::execute(RequestContext& ctx) {
    for (auto& middleware : middleware_) {
        MiddlewareResult result = middleware->process(ctx);

        if (result == MiddlewareResult::Stop) {
            return MiddlewareResult::Stop;
        }

        if (result == MiddlewareResult::Error || ctx.has_error) {
            return MiddlewareResult::Error;
        }
    }

    return MiddlewareResult::Continue;
}

} // namespace titan::gateway
