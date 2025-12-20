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

// Transform Middleware - Unit Tests

#include <catch2/catch_test_macros.hpp>

#include "../../src/control/config.hpp"
#include "../../src/gateway/transform_middleware.hpp"
#include "../../src/http/http.hpp"
#include "../../src/http/regex.hpp"

using namespace titan;
using namespace titan::gateway;
using namespace titan::control;

TEST_CASE("TransformMiddleware - Path Prefix Stripping", "[transform][path]") {
    TransformConfig config;
    config.enabled = true;
    config.path_rewrites.push_back({"prefix_strip", "/api/v1", ""});

    auto middleware = std::make_unique<TransformMiddleware>(config);

    SECTION("Strip matching prefix") {
        http::Request req;
        req.method = http::Method::GET;
        req.path = "/api/v1/users/123";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(ctx.get_metadata("transformed_path") == "/users/123");
    }

    SECTION("No match - path unchanged") {
        http::Request req;
        req.method = http::Method::GET;
        req.path = "/public/health";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(ctx.get_metadata("transformed_path").empty());
    }

    SECTION("Strip prefix resulting in root path") {
        http::Request req;
        req.method = http::Method::GET;
        req.path = "/api/v1";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(ctx.get_metadata("transformed_path") == "/");
    }
}

TEST_CASE("TransformMiddleware - Path Regex Substitution", "[transform][path][regex]") {
    TransformConfig config;
    config.enabled = true;
    config.path_rewrites.push_back({"regex", "/old/(.*)", "/new/$1"});

    auto middleware = std::make_unique<TransformMiddleware>(config);

    SECTION("Regex with capture group") {
        http::Request req;
        req.method = http::Method::GET;
        req.path = "/old/users/123";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(ctx.get_metadata("transformed_path") == "/new/users/123");
    }

    SECTION("No match - path unchanged") {
        http::Request req;
        req.method = http::Method::GET;
        req.path = "/public/health";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(ctx.get_metadata("transformed_path").empty());
    }
}

TEST_CASE("TransformMiddleware - Multiple Path Transformations", "[transform][path]") {
    TransformConfig config;
    config.enabled = true;
    // First strip /api, then apply regex
    config.path_rewrites.push_back({"prefix_strip", "/api", ""});
    config.path_rewrites.push_back({"regex", "/v1/(.*)", "/$1"});

    auto middleware = std::make_unique<TransformMiddleware>(config);

    http::Request req;
    req.method = http::Method::GET;
    req.path = "/api/v1/users";

    http::Response res;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;

    auto result = middleware->process_request(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    // After prefix strip: /v1/users
    // After regex: /users
    REQUIRE(ctx.get_metadata("transformed_path") == "/users");
}

TEST_CASE("TransformMiddleware - Request Header Manipulation", "[transform][headers]") {
    TransformConfig config;
    config.enabled = true;

    SECTION("Add request header") {
        config.request_headers.push_back({"add", "X-API-Version", "v1"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Check header_transforms instead of metadata (zero-copy optimization)
        REQUIRE(ctx.header_transforms.has_value());
        REQUIRE(ctx.header_transforms->add.size() == 1);
        REQUIRE(ctx.header_transforms->add[0].first == "X-API-Version");
        REQUIRE(ctx.header_transforms->add[0].second == "v1");
    }

    SECTION("Remove request header") {
        config.request_headers.push_back({"remove", "Authorization", ""});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Check header_transforms instead of metadata (zero-copy optimization)
        REQUIRE(ctx.header_transforms.has_value());
        REQUIRE(ctx.header_transforms->remove.size() == 1);
        REQUIRE(ctx.header_transforms->remove[0] == "Authorization");
    }

    SECTION("Modify request header") {
        config.request_headers.push_back({"modify", "Host", "backend.internal"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Check header_transforms instead of metadata (zero-copy optimization)
        REQUIRE(ctx.header_transforms.has_value());
        REQUIRE(ctx.header_transforms->modify.size() == 1);
        REQUIRE(ctx.header_transforms->modify[0].first == "Host");
        REQUIRE(ctx.header_transforms->modify[0].second == "backend.internal");
    }
}

TEST_CASE("TransformMiddleware - Response Header Manipulation", "[transform][headers]") {
    TransformConfig config;
    config.enabled = true;

    SECTION("Add response header") {
        config.response_headers.push_back({"add", "X-Powered-By", "Titan"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        http::Response res;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.has_header("X-Powered-By"));
        REQUIRE(res.get_header("X-Powered-By") == "Titan");
    }

    SECTION("Remove response header") {
        config.response_headers.push_back({"remove", "Server", ""});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        http::Response res;
        res.headers.push_back({"Server", "nginx/1.21"});

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE_FALSE(res.has_header("Server"));
    }

    SECTION("Modify response header") {
        config.response_headers.push_back({"modify", "Cache-Control", "public, max-age=3600"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        http::Response res;
        res.headers.push_back({"Cache-Control", "private"});

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Cache-Control") == "public, max-age=3600");
    }

    SECTION("Modify non-existent header - should add it") {
        config.response_headers.push_back({"modify", "X-Custom", "new-value"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        http::Response res;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.has_header("X-Custom"));
        REQUIRE(res.get_header("X-Custom") == "new-value");
    }
}

TEST_CASE("TransformMiddleware - Query Parameter Manipulation", "[transform][query]") {
    TransformConfig config;
    config.enabled = true;

    SECTION("Add query parameter to empty query") {
        config.query_params.push_back({"add", "source", "titan"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";
        req.query = "";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Empty query means no transformation
        REQUIRE(ctx.get_metadata("transformed_query").empty());
    }

    SECTION("Add query parameter to existing query") {
        config.query_params.push_back({"add", "api_key", "secret123"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";
        req.query = "limit=10";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        auto transformed = ctx.get_metadata("transformed_query");
        REQUIRE_FALSE(transformed.empty());
        REQUIRE(transformed.find("limit=10") != std::string::npos);
        REQUIRE(transformed.find("api_key=secret123") != std::string::npos);
    }

    SECTION("Remove query parameter") {
        config.query_params.push_back({"remove", "debug", ""});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";
        req.query = "limit=10&debug=true&offset=5";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        auto transformed = ctx.get_metadata("transformed_query");
        REQUIRE_FALSE(transformed.empty());
        REQUIRE(transformed.find("limit=10") != std::string::npos);
        REQUIRE(transformed.find("offset=5") != std::string::npos);
        REQUIRE(transformed.find("debug") == std::string::npos);
    }

    SECTION("Modify query parameter") {
        config.query_params.push_back({"modify", "version", "2"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/api";
        req.query = "version=1&limit=10";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        auto transformed = ctx.get_metadata("transformed_query");
        REQUIRE_FALSE(transformed.empty());
        REQUIRE(transformed.find("version=2") != std::string::npos);
        REQUIRE(transformed.find("version=1") == std::string::npos);
        REQUIRE(transformed.find("limit=10") != std::string::npos);
    }
}

TEST_CASE("TransformMiddleware - Config Merging", "[transform][config]") {
    SECTION("Global config only") {
        TransformConfig global_config;
        global_config.enabled = true;
        global_config.request_headers.push_back({"add", "X-Global", "1"});

        auto middleware = std::make_unique<TransformMiddleware>(global_config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        // No per-route config

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Check header_transforms instead of metadata (zero-copy optimization)
        REQUIRE(ctx.header_transforms.has_value());
        REQUIRE(ctx.header_transforms->add.size() == 1);
        REQUIRE(ctx.header_transforms->add[0].first == "X-Global");
        REQUIRE(ctx.header_transforms->add[0].second == "1");
    }

    SECTION("Per-route config overrides global") {
        TransformConfig global_config;
        global_config.enabled = true;
        global_config.request_headers.push_back({"add", "X-Global", "1"});

        auto middleware = std::make_unique<TransformMiddleware>(global_config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        // Per-route config
        TransformConfig route_config;
        route_config.enabled = true;
        route_config.request_headers.push_back({"add", "X-Route", "2"});
        ctx.route_match.transform_config = route_config;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Per-route overrides global completely - check header_transforms
        REQUIRE(ctx.header_transforms.has_value());
        REQUIRE(ctx.header_transforms->add.size() == 1);
        REQUIRE(ctx.header_transforms->add[0].first == "X-Route");
        REQUIRE(ctx.header_transforms->add[0].second == "2");
    }

    SECTION("Disabled transform - no transformation") {
        TransformConfig global_config;
        global_config.enabled = false;  // Disabled
        global_config.request_headers.push_back({"add", "X-Test", "1"});

        auto middleware = std::make_unique<TransformMiddleware>(global_config);

        http::Request req;
        req.method = http::Method::GET;
        req.path = "/users";

        http::Response res;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_request(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // When disabled, header_transforms should not be populated
        REQUIRE(!ctx.header_transforms.has_value());
    }
}

TEST_CASE("TransformMiddleware - Memory Safety", "[transform][memory]") {
    SECTION("Response header values stored in metadata") {
        TransformConfig config;
        config.enabled = true;
        config.response_headers.push_back({"add", "X-Test", "temporary_value"});

        auto middleware = std::make_unique<TransformMiddleware>(config);

        http::Request req;
        http::Response res;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware->process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);

        // Verify header exists
        REQUIRE(res.has_header("X-Test"));
        auto header_value = res.get_header("X-Test");

        // Verify value is correct
        REQUIRE(header_value == "temporary_value");

        // With hybrid storage, headers are now owned by middleware_headers vector
        // No need to check metadata - add_middleware_header() copies to owned storage
    }
}

TEST_CASE("URL Encoding/Decoding", "[transform][url]") {
    SECTION("URL encode special characters") {
        auto encoded = titan::http::url::encode("hello world");
        REQUIRE(encoded == "hello%20world");
    }

    SECTION("URL encode path with slashes") {
        auto encoded = titan::http::url::encode("/path/to/resource");
        REQUIRE(encoded == "%2Fpath%2Fto%2Fresource");
    }

    SECTION("URL decode percent-encoded string") {
        auto decoded = titan::http::url::decode("hello%20world");
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == "hello world");
    }

    SECTION("URL decode invalid encoding") {
        auto decoded = titan::http::url::decode("invalid%2");  // Incomplete percent sequence
        REQUIRE_FALSE(decoded.has_value());
    }

    SECTION("URL decode plus sign as space") {
        auto decoded = titan::http::url::decode("hello+world");
        REQUIRE(decoded.has_value());
        REQUIRE(*decoded == "hello world");
    }
}

TEST_CASE("Regex Compilation and Caching", "[transform][regex]") {
    SECTION("Valid regex pattern compiles") {
        auto regex = titan::http::Regex::compile("/api/(.*)");
        REQUIRE(regex.has_value());
    }

    SECTION("Invalid regex pattern fails") {
        std::string error_msg;
        auto regex = titan::http::Regex::compile("([unclosed", error_msg);
        REQUIRE_FALSE(regex.has_value());
        REQUIRE_FALSE(error_msg.empty());
    }

    SECTION("Regex matches") {
        auto regex = titan::http::Regex::compile("/api/(.*)");
        REQUIRE(regex.has_value());
        REQUIRE(regex->matches("/api/users"));
        REQUIRE_FALSE(regex->matches("/public/health"));
    }

    SECTION("Regex substitution with capture groups") {
        auto regex = titan::http::Regex::compile("/old/(.*)");
        REQUIRE(regex.has_value());

        auto result = regex->substitute("/old/users/123", "/new/$1");
        REQUIRE(result == "/new/users/123");
    }

    SECTION("Regex extract groups") {
        auto regex = titan::http::Regex::compile("/api/([^/]+)/([^/]+)");
        REQUIRE(regex.has_value());

        auto groups = regex->extract_groups("/api/users/123");
        REQUIRE(groups.size() == 3);  // Full match + 2 capture groups
        REQUIRE(groups[0] == "/api/users/123");
        REQUIRE(groups[1] == "users");
        REQUIRE(groups[2] == "123");
    }
}
