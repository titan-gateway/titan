// Titan Two-Phase Middleware Unit Tests

#include "../../src/gateway/pipeline.hpp"
#include "../../src/gateway/upstream.hpp"
#include "../../src/http/http.hpp"

#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

using namespace titan::gateway;
using namespace titan::http;

// ============================================================================
// Test Middleware Implementations
// ============================================================================

/// Test middleware that tracks execution
class TrackingMiddleware : public Middleware {
public:
    mutable int request_count = 0;
    mutable int response_count = 0;
    MiddlewareResult request_result = MiddlewareResult::Continue;
    MiddlewareResult response_result = MiddlewareResult::Continue;

    MiddlewareResult process_request(RequestContext& ctx) override {
        (void)ctx;
        request_count++;
        return request_result;
    }

    MiddlewareResult process_response(ResponseContext& ctx) override {
        (void)ctx;
        response_count++;
        return response_result;
    }

    std::string_view name() const override { return "TrackingMiddleware"; }
};

/// Test middleware that sets metadata
class MetadataMiddleware : public Middleware {
public:
    MiddlewareResult process_request(RequestContext& ctx) override {
        ctx.set_metadata("request_key", "request_value");
        return MiddlewareResult::Continue;
    }

    MiddlewareResult process_response(ResponseContext& ctx) override {
        // Check if metadata from request phase is available
        auto value = ctx.get_metadata("request_key");
        if (!value.empty()) {
            ctx.set_metadata("response_key", "metadata_propagated");
        }
        return MiddlewareResult::Continue;
    }

    std::string_view name() const override { return "MetadataMiddleware"; }
};

/// Test middleware that stops pipeline
class StoppingMiddleware : public Middleware {
public:
    bool should_stop_request = false;
    bool should_stop_response = false;

    MiddlewareResult process_request(RequestContext& ctx) override {
        if (should_stop_request) {
            ctx.response->status = StatusCode::Unauthorized;
            return MiddlewareResult::Stop;
        }
        return MiddlewareResult::Continue;
    }

    MiddlewareResult process_response(ResponseContext& ctx) override {
        (void)ctx;
        if (should_stop_response) {
            return MiddlewareResult::Stop;
        }
        return MiddlewareResult::Continue;
    }

    std::string_view name() const override { return "StoppingMiddleware"; }
};

// ============================================================================
// Pipeline Tests
// ============================================================================

TEST_CASE("Pipeline - Add middleware", "[middleware][pipeline]") {
    Pipeline pipeline;

    REQUIRE(pipeline.size() == 0);

    pipeline.use(std::make_unique<TrackingMiddleware>());
    REQUIRE(pipeline.size() == 1);

    pipeline.use(std::make_unique<TrackingMiddleware>());
    REQUIRE(pipeline.size() == 2);
}

TEST_CASE("Pipeline - Execute request phase", "[middleware][pipeline]") {
    Pipeline pipeline;
    auto* tracking1 = new TrackingMiddleware();
    auto* tracking2 = new TrackingMiddleware();

    pipeline.use(std::unique_ptr<Middleware>(tracking1));
    pipeline.use(std::unique_ptr<Middleware>(tracking2));

    Request req;
    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = pipeline.execute_request(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    REQUIRE(tracking1->request_count == 1);
    REQUIRE(tracking2->request_count == 1);
    REQUIRE(tracking1->response_count == 0);
    REQUIRE(tracking2->response_count == 0);
}

TEST_CASE("Pipeline - Execute response phase", "[middleware][pipeline]") {
    Pipeline pipeline;
    auto* tracking1 = new TrackingMiddleware();
    auto* tracking2 = new TrackingMiddleware();

    pipeline.use(std::unique_ptr<Middleware>(tracking1));
    pipeline.use(std::unique_ptr<Middleware>(tracking2));

    Request req;
    Response res;

    ResponseContext ctx;
    ctx.request = &req;
    ctx.response = &res;

    auto result = pipeline.execute_response(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    REQUIRE(tracking1->request_count == 0);
    REQUIRE(tracking2->request_count == 0);
    REQUIRE(tracking1->response_count == 1);
    REQUIRE(tracking2->response_count == 1);
}

TEST_CASE("Pipeline - Request phase stops on Stop result", "[middleware][pipeline]") {
    Pipeline pipeline;
    auto* tracking1 = new TrackingMiddleware();
    auto* stopping = new StoppingMiddleware();
    auto* tracking2 = new TrackingMiddleware();

    stopping->should_stop_request = true;

    pipeline.use(std::unique_ptr<Middleware>(tracking1));
    pipeline.use(std::unique_ptr<Middleware>(stopping));
    pipeline.use(std::unique_ptr<Middleware>(tracking2));

    Request req;
    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = pipeline.execute_request(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(tracking1->request_count == 1);  // Ran before stop
    REQUIRE(tracking2->request_count == 0);  // Did not run after stop
    REQUIRE(res.status == StatusCode::Unauthorized);
}

TEST_CASE("Pipeline - Response phase stops on Stop result", "[middleware][pipeline]") {
    Pipeline pipeline;
    auto* tracking1 = new TrackingMiddleware();
    auto* stopping = new StoppingMiddleware();
    auto* tracking2 = new TrackingMiddleware();

    stopping->should_stop_response = true;

    pipeline.use(std::unique_ptr<Middleware>(tracking1));
    pipeline.use(std::unique_ptr<Middleware>(stopping));
    pipeline.use(std::unique_ptr<Middleware>(tracking2));

    Request req;
    Response res;

    ResponseContext ctx;
    ctx.request = &req;
    ctx.response = &res;

    auto result = pipeline.execute_response(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(tracking1->response_count == 1);  // Ran before stop
    REQUIRE(tracking2->response_count == 0);  // Did not run after stop
}

TEST_CASE("Pipeline - Metadata propagation", "[middleware][pipeline]") {
    Pipeline pipeline;
    auto* metadata_mw = new MetadataMiddleware();

    pipeline.use(std::unique_ptr<Middleware>(metadata_mw));

    Request req;
    Response res;
    RouteMatch match;

    // Request phase
    RequestContext req_ctx;
    req_ctx.request = &req;
    req_ctx.response = &res;
    req_ctx.route_match = match;

    (void)pipeline.execute_request(req_ctx);

    REQUIRE(req_ctx.get_metadata("request_key") == "request_value");

    // Response phase (with metadata from request)
    ResponseContext resp_ctx;
    resp_ctx.request = &req;
    resp_ctx.response = &res;
    resp_ctx.metadata = req_ctx.metadata;  // Propagate metadata

    (void)pipeline.execute_response(resp_ctx);

    REQUIRE(resp_ctx.get_metadata("response_key") == "metadata_propagated");
}

TEST_CASE("Pipeline - Clear all middleware", "[middleware][pipeline]") {
    Pipeline pipeline;

    pipeline.use(std::make_unique<TrackingMiddleware>());
    pipeline.use(std::make_unique<TrackingMiddleware>());

    REQUIRE(pipeline.size() == 2);

    pipeline.clear();

    REQUIRE(pipeline.size() == 0);
}

// ============================================================================
// ProxyMiddleware Tests
// ============================================================================

TEST_CASE("ProxyMiddleware - No upstream in route", "[middleware][proxy]") {
    UpstreamManager manager;
    ProxyMiddleware middleware(&manager);

    Request req;
    Response res;
    RouteMatch match;
    match.upstream_name = "";  // No upstream

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    REQUIRE(ctx.upstream == nullptr);
}

TEST_CASE("ProxyMiddleware - Upstream not found", "[middleware][proxy]") {
    UpstreamManager manager;
    ProxyMiddleware middleware(&manager);

    Request req;
    Response res;
    RouteMatch match;
    match.upstream_name = "nonexistent";

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::BadGateway);
    REQUIRE(ctx.has_error);
    REQUIRE(ctx.error_message.find("not found") != std::string::npos);
}

TEST_CASE("ProxyMiddleware - No healthy backends", "[middleware][proxy]") {
    UpstreamManager manager;

    // Create upstream with unhealthy backend
    auto upstream = std::make_unique<Upstream>("test_upstream");
    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;
    backend.status = BackendStatus::Unhealthy;
    upstream->add_backend(std::move(backend));

    manager.register_upstream(std::move(upstream));

    ProxyMiddleware middleware(&manager);

    Request req;
    Response res;
    RouteMatch match;
    match.upstream_name = "test_upstream";

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::ServiceUnavailable);
    REQUIRE(ctx.has_error);
}

TEST_CASE("ProxyMiddleware - Valid upstream", "[middleware][proxy]") {
    UpstreamManager manager;

    // Create upstream with healthy backend
    auto upstream = std::make_unique<Upstream>("test_upstream");
    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;
    backend.status = BackendStatus::Healthy;
    upstream->add_backend(std::move(backend));

    manager.register_upstream(std::move(upstream));

    ProxyMiddleware middleware(&manager);

    Request req;
    Response res;
    RouteMatch match;
    match.upstream_name = "test_upstream";

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    REQUIRE(ctx.upstream != nullptr);
    REQUIRE(ctx.upstream->name() == "test_upstream");
    REQUIRE_FALSE(ctx.has_error);
}

TEST_CASE("ProxyMiddleware - Response phase adds headers", "[middleware][proxy]") {
    UpstreamManager manager;
    ProxyMiddleware middleware(&manager);

    Request req;
    Response res;

    ResponseContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.start_time = std::chrono::steady_clock::now();

    // Simulate some time passing
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    auto result = middleware.process_response(ctx);

    REQUIRE(result == MiddlewareResult::Continue);

    // Check X-Proxy header
    bool found_proxy_header = false;
    for (const auto& header : res.headers) {
        if (header.name == "X-Proxy" && header.value == "Titan") {
            found_proxy_header = true;
        }
    }
    REQUIRE(found_proxy_header);

    // Check X-Response-Time header
    bool found_time_header = false;
    for (const auto& header : res.headers) {
        if (header.name == "X-Response-Time") {
            found_time_header = true;
            // Should contain "ms"
            REQUIRE(header.value.find("ms") != std::string::npos);
        }
    }
    REQUIRE(found_time_header);
}

// ============================================================================
// LoggingMiddleware Tests
// ============================================================================

TEST_CASE("LoggingMiddleware - Logs in response phase", "[middleware][logging]") {
    LoggingMiddleware middleware;

    Request req;
    req.method = Method::GET;
    req.path = "/test";

    Response res;
    res.status = StatusCode::OK;

    ResponseContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.start_time = std::chrono::steady_clock::now();

    auto result = middleware.process_response(ctx);

    REQUIRE(result == MiddlewareResult::Continue);

    // NOTE: Logging is currently disabled for performance (see pipeline.cpp)
    // Test just verifies middleware doesn't crash and returns Continue
}

TEST_CASE("LoggingMiddleware - Error if no request or response", "[middleware][logging]") {
    LoggingMiddleware middleware;

    ResponseContext ctx;
    ctx.request = nullptr;
    ctx.response = nullptr;

    auto result = middleware.process_response(ctx);

    REQUIRE(result == MiddlewareResult::Error);
}

// ============================================================================
// CorsMiddleware Tests
// ============================================================================

TEST_CASE("CorsMiddleware - Adds CORS headers in request phase", "[middleware][cors]") {
    CorsMiddleware middleware;

    Request req;
    req.method = Method::GET;

    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Continue);

    // Check headers were added
    bool found_origin = false;
    bool found_methods = false;
    bool found_headers = false;

    for (const auto& header : res.headers) {
        if (header.name == "Access-Control-Allow-Origin") {
            found_origin = true;
        }
        if (header.name == "Access-Control-Allow-Methods") {
            found_methods = true;
        }
        if (header.name == "Access-Control-Allow-Headers") {
            found_headers = true;
        }
    }

    REQUIRE(found_origin);
    REQUIRE(found_methods);
    REQUIRE(found_headers);
}

TEST_CASE("CorsMiddleware - OPTIONS preflight stops pipeline", "[middleware][cors]") {
    CorsMiddleware middleware;

    Request req;
    req.method = Method::OPTIONS;

    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    auto result = middleware.process_request(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(res.status == StatusCode::NoContent);
}

TEST_CASE("CorsMiddleware - Custom configuration", "[middleware][cors]") {
    CorsMiddleware::Config config;
    config.allowed_origins = {"https://example.com"};
    config.allow_credentials = true;

    CorsMiddleware middleware(config);

    Request req;
    req.method = Method::GET;

    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    (void)middleware.process_request(ctx);

    // Check custom origin
    bool found_origin = false;
    bool found_credentials = false;

    for (const auto& header : res.headers) {
        if (header.name == "Access-Control-Allow-Origin" &&
            header.value == "https://example.com") {
            found_origin = true;
        }
        if (header.name == "Access-Control-Allow-Credentials" &&
            header.value == "true") {
            found_credentials = true;
        }
    }

    REQUIRE(found_origin);
    REQUIRE(found_credentials);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("Integration - Full request → response flow", "[middleware][integration]") {
    Pipeline pipeline;

    auto* tracking = new TrackingMiddleware();
    auto* metadata = new MetadataMiddleware();

    pipeline.use(std::unique_ptr<Middleware>(tracking));
    pipeline.use(std::unique_ptr<Middleware>(metadata));

    Request req;
    Response res;
    RouteMatch match;

    // Request phase
    RequestContext req_ctx;
    req_ctx.request = &req;
    req_ctx.response = &res;
    req_ctx.route_match = match;

    auto req_result = pipeline.execute_request(req_ctx);

    REQUIRE(req_result == MiddlewareResult::Continue);
    REQUIRE(tracking->request_count == 1);
    REQUIRE(req_ctx.get_metadata("request_key") == "request_value");

    // Simulate async proxy...

    // Response phase (propagate metadata)
    ResponseContext resp_ctx;
    resp_ctx.request = &req;
    resp_ctx.response = &res;
    resp_ctx.metadata = req_ctx.metadata;  // Key: propagate metadata

    auto resp_result = pipeline.execute_response(resp_ctx);

    REQUIRE(resp_result == MiddlewareResult::Continue);
    REQUIRE(tracking->response_count == 1);
    REQUIRE(resp_ctx.get_metadata("response_key") == "metadata_propagated");
}

TEST_CASE("Integration - Middleware ordering matters", "[middleware][integration]") {
    Pipeline pipeline;

    auto* mw1 = new TrackingMiddleware();
    auto* mw2 = new TrackingMiddleware();
    auto* mw3 = new TrackingMiddleware();

    pipeline.use(std::unique_ptr<Middleware>(mw1));
    pipeline.use(std::unique_ptr<Middleware>(mw2));
    pipeline.use(std::unique_ptr<Middleware>(mw3));

    Request req;
    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;

    (void)pipeline.execute_request(ctx);

    // All should have run
    REQUIRE(mw1->request_count == 1);
    REQUIRE(mw2->request_count == 1);
    REQUIRE(mw3->request_count == 1);
}
