// Titan Pipeline - Header
// Middleware chain for request processing

#pragma once

#include "../http/http.hpp"
#include "rate_limit.hpp"
#include "router.hpp"
#include "upstream.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace titan::gateway {

/// Request context (passed through middleware chain)
struct RequestContext {
    // Request/Response
    http::Request* request = nullptr;
    http::Response* response = nullptr;

    // Routing
    RouteMatch route_match;

    // Connection info
    std::string client_ip;
    uint16_t client_port = 0;

    // Upstream
    Upstream* upstream = nullptr;
    Connection* connection = nullptr;

    // Metadata (for middleware communication)
    std::unordered_map<std::string, std::string> metadata;

    // Timing
    std::chrono::steady_clock::time_point start_time;

    // Error handling
    bool has_error = false;
    std::string error_message;

    /// Helper: Set error
    void set_error(std::string message) {
        has_error = true;
        error_message = std::move(message);
    }

    /// Helper: Get metadata
    [[nodiscard]] std::string_view get_metadata(std::string_view key) const {
        auto it = metadata.find(std::string(key));
        return (it != metadata.end()) ? std::string_view(it->second) : std::string_view{};
    }

    /// Helper: Set metadata
    void set_metadata(std::string key, std::string value) {
        metadata[std::move(key)] = std::move(value);
    }
};

/// Middleware result
enum class MiddlewareResult {
    Continue,  // Continue to next middleware
    Stop,      // Stop pipeline execution
    Error      // Error occurred
};

/// Middleware function signature
/// Returns true to continue pipeline, false to stop
using MiddlewareFunc = std::function<MiddlewareResult(RequestContext&)>;

/// Middleware base class
class Middleware {
public:
    virtual ~Middleware() = default;

    /// Process request
    [[nodiscard]] virtual MiddlewareResult process(RequestContext& ctx) = 0;

    /// Get middleware name (for debugging)
    [[nodiscard]] virtual std::string_view name() const = 0;
};

/// Logging middleware
class LoggingMiddleware : public Middleware {
public:
    MiddlewareResult process(RequestContext& ctx) override;
    std::string_view name() const override { return "LoggingMiddleware"; }
};

/// CORS middleware
class CorsMiddleware : public Middleware {
public:
    struct Config {
        std::vector<std::string> allowed_origins;
        std::vector<std::string> allowed_methods;
        std::vector<std::string> allowed_headers;
        bool allow_credentials;
        int max_age;

        Config()
            : allowed_origins{"*"}
            , allowed_methods{"GET", "POST", "PUT", "DELETE", "OPTIONS"}
            , allowed_headers{"*"}
            , allow_credentials(false)
            , max_age(86400) {}
    };

    CorsMiddleware() : config_() {}
    explicit CorsMiddleware(Config config) : config_(std::move(config)) {}

    MiddlewareResult process(RequestContext& ctx) override;
    std::string_view name() const override { return "CorsMiddleware"; }

private:
    Config config_;
};

/// Rate limiting middleware (thread-local token bucket)
class RateLimitMiddleware : public Middleware {
public:
    struct Config {
        size_t requests_per_second;
        size_t burst_size;

        Config()
            : requests_per_second(100)
            , burst_size(200) {}
    };

    RateLimitMiddleware();
    explicit RateLimitMiddleware(Config config);

    MiddlewareResult process(RequestContext& ctx) override;
    std::string_view name() const override { return "RateLimitMiddleware"; }

private:
    Config config_;
    std::unique_ptr<ThreadLocalRateLimiter> limiter_;
};

/// Proxy middleware (forwards request to upstream)
class ProxyMiddleware : public Middleware {
public:
    explicit ProxyMiddleware(UpstreamManager* manager) : upstream_manager_(manager) {}

    MiddlewareResult process(RequestContext& ctx) override;
    std::string_view name() const override { return "ProxyMiddleware"; }

private:
    UpstreamManager* upstream_manager_;
};

/// Middleware pipeline
class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    // Non-copyable, movable
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    /// Add middleware to pipeline
    void use(std::unique_ptr<Middleware> middleware);

    /// Add middleware function to pipeline
    void use(MiddlewareFunc func, std::string_view name = "CustomMiddleware");

    /// Execute pipeline
    [[nodiscard]] MiddlewareResult execute(RequestContext& ctx);

    /// Get middleware count
    [[nodiscard]] size_t size() const noexcept { return middleware_.size(); }

    /// Clear all middleware
    void clear() { middleware_.clear(); }

private:
    std::vector<std::unique_ptr<Middleware>> middleware_;
};

/// Pipeline builder (fluent API)
class PipelineBuilder {
public:
    PipelineBuilder() = default;

    PipelineBuilder& use(std::unique_ptr<Middleware> middleware) {
        pipeline_.use(std::move(middleware));
        return *this;
    }

    PipelineBuilder& use(MiddlewareFunc func, std::string_view name = "CustomMiddleware") {
        pipeline_.use(std::move(func), name);
        return *this;
    }

    Pipeline build() && {
        return std::move(pipeline_);
    }

private:
    Pipeline pipeline_;
};

/// Function middleware wrapper
class FunctionMiddleware : public Middleware {
public:
    explicit FunctionMiddleware(MiddlewareFunc func, std::string name)
        : func_(std::move(func)), name_(std::move(name)) {}

    MiddlewareResult process(RequestContext& ctx) override {
        return func_(ctx);
    }

    std::string_view name() const override { return name_; }

private:
    MiddlewareFunc func_;
    std::string name_;
};

} // namespace titan::gateway
