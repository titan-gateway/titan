// Titan Gateway Layer Unit Tests

#include "../../src/gateway/router.hpp"
#include "../../src/gateway/upstream.hpp"
#include "../../src/gateway/pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace titan::gateway;
using namespace titan::http;

TEST_CASE("Router exact path matching", "[gateway][router]") {
    Router router;

    Route route;
    route.path = "/hello";
    route.method = Method::GET;
    route.handler_id = "handler1";

    router.add_route(std::move(route));

    SECTION("Exact match") {
        auto match = router.match(Method::GET, "/hello");
        REQUIRE(match.matched());
        REQUIRE(match.handler_id == "handler1");
        REQUIRE(match.params.empty());
    }

    SECTION("No match - different path") {
        auto match = router.match(Method::GET, "/goodbye");
        REQUIRE_FALSE(match.matched());
    }

    SECTION("No match - different method") {
        auto match = router.match(Method::POST, "/hello");
        REQUIRE_FALSE(match.matched());
    }
}

TEST_CASE("Router path parameters", "[gateway][router]") {
    Router router;

    Route route;
    route.path = "/users/:id";
    route.method = Method::GET;
    route.handler_id = "get_user";

    router.add_route(std::move(route));

    SECTION("Match with parameter") {
        auto match = router.match(Method::GET, "/users/123");
        REQUIRE(match.matched());
        REQUIRE(match.handler_id == "get_user");
        REQUIRE(match.params.size() == 1);
        REQUIRE(match.params[0].name == "id");
        REQUIRE(match.params[0].value == "123");
    }

    SECTION("Parameter helper") {
        auto match = router.match(Method::GET, "/users/456");
        auto id = match.get_param("id");
        REQUIRE(id.has_value());
        REQUIRE(id.value() == "456");
    }

    SECTION("Multiple parameters") {
        Router router2;
        Route route2;
        route2.path = "/users/:userId/posts/:postId";
        route2.method = Method::GET;
        route2.handler_id = "get_post";
        router2.add_route(std::move(route2));

        auto match = router2.match(Method::GET, "/users/42/posts/100");
        REQUIRE(match.matched());
        REQUIRE(match.params.size() == 2);

        auto userId = match.get_param("userId");
        auto postId = match.get_param("postId");
        REQUIRE(userId.has_value());
        REQUIRE(userId.value() == "42");
        REQUIRE(postId.has_value());
        REQUIRE(postId.value() == "100");
    }
}

TEST_CASE("Router wildcard matching", "[gateway][router]") {
    Router router;

    Route route;
    route.path = "/static/*";
    route.method = Method::GET;
    route.handler_id = "static_files";

    router.add_route(std::move(route));

    auto match = router.match(Method::GET, "/static/css/style.css");
    REQUIRE(match.matched());
    REQUIRE(match.handler_id == "static_files");
    REQUIRE(match.wildcard == "css/style.css");
}

TEST_CASE("Router multiple routes", "[gateway][router]") {
    Router router;

    // Add multiple routes
    router.add_route(Route{"/", Method::GET, "index"});
    router.add_route(Route{"/about", Method::GET, "about"});
    router.add_route(Route{"/users", Method::GET, "list_users"});
    router.add_route(Route{"/users/:id", Method::GET, "get_user"});
    router.add_route(Route{"/users", Method::POST, "create_user"});

    SECTION("Match different routes") {
        REQUIRE(router.match(Method::GET, "/").handler_id == "index");
        REQUIRE(router.match(Method::GET, "/about").handler_id == "about");
        REQUIRE(router.match(Method::GET, "/users").handler_id == "list_users");
        REQUIRE(router.match(Method::POST, "/users").handler_id == "create_user");
        REQUIRE(router.match(Method::GET, "/users/42").handler_id == "get_user");
    }
}

TEST_CASE("Router stats", "[gateway][router]") {
    Router router;

    router.add_route(Route{"/a", Method::GET, "a"});
    router.add_route(Route{"/b", Method::GET, "b"});
    router.add_route(Route{"/c", Method::GET, "c"});

    auto stats = router.get_stats();
    REQUIRE(stats.total_routes == 3);
    REQUIRE(stats.total_nodes > 0);
}

TEST_CASE("Extract param names from pattern", "[gateway][router]") {
    auto params = extract_param_names("/users/:id");
    REQUIRE(params.size() == 1);
    REQUIRE(params[0] == "id");

    auto params2 = extract_param_names("/users/:userId/posts/:postId");
    REQUIRE(params2.size() == 2);
    REQUIRE(params2[0] == "userId");
    REQUIRE(params2[1] == "postId");

    auto params3 = extract_param_names("/no/params");
    REQUIRE(params3.empty());
}

TEST_CASE("Backend availability", "[gateway][upstream]") {
    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;
    backend.status = BackendStatus::Healthy;
    backend.max_connections = 10;
    backend.active_connections = 5;

    REQUIRE(backend.is_available());
    REQUIRE(backend.can_accept_connection());

    SECTION("Unhealthy backend") {
        backend.status = BackendStatus::Unhealthy;
        REQUIRE_FALSE(backend.is_available());
        REQUIRE_FALSE(backend.can_accept_connection());
    }

    SECTION("Max connections reached") {
        backend.active_connections = 10;
        REQUIRE(backend.is_available());
        REQUIRE_FALSE(backend.can_accept_connection());
    }

    SECTION("Backend address") {
        REQUIRE(backend.address() == "localhost:8080");
    }
}

TEST_CASE("Round-robin load balancer", "[gateway][upstream]") {
    std::vector<Backend> backends;
    for (int i = 0; i < 3; ++i) {
        Backend b;
        b.host = "backend" + std::to_string(i);
        b.port = 8080 + i;
        b.status = BackendStatus::Healthy;
        backends.push_back(std::move(b));
    }

    RoundRobinBalancer balancer;

    // Should cycle through backends
    std::vector<Backend*> selected;
    for (int i = 0; i < 6; ++i) {
        Backend* b = balancer.select(backends, "");
        REQUIRE(b != nullptr);
        selected.push_back(b);
    }

    // Check round-robin pattern (should repeat after 3)
    REQUIRE(selected[0]->host == selected[3]->host);
    REQUIRE(selected[1]->host == selected[4]->host);
    REQUIRE(selected[2]->host == selected[5]->host);
}

TEST_CASE("Least connections load balancer", "[gateway][upstream]") {
    std::vector<Backend> backends;
    for (int i = 0; i < 3; ++i) {
        Backend b;
        b.host = "backend" + std::to_string(i);
        b.port = 8080 + i;
        b.status = BackendStatus::Healthy;
        b.active_connections = i * 5; // 0, 5, 10
        backends.push_back(std::move(b));
    }

    LeastConnectionsBalancer balancer;

    Backend* selected = balancer.select(backends, "");
    REQUIRE(selected != nullptr);
    REQUIRE(selected->host == "backend0"); // Has 0 connections
}

TEST_CASE("Weighted round-robin load balancer", "[gateway][upstream]") {
    std::vector<Backend> backends;

    // Backend 0: weight = 3
    Backend b0;
    b0.host = "backend0";
    b0.port = 8080;
    b0.weight = 3;
    b0.status = BackendStatus::Healthy;
    backends.push_back(std::move(b0));

    // Backend 1: weight = 1
    Backend b1;
    b1.host = "backend1";
    b1.port = 8081;
    b1.weight = 1;
    b1.status = BackendStatus::Healthy;
    backends.push_back(std::move(b1));

    WeightedRoundRobinBalancer balancer;

    // Select 8 times to see the distribution
    // Expected pattern: backend0 appears 3x more than backend1
    // Pool: [b0, b0, b0, b1] → repeats
    std::map<std::string, int> selection_counts;
    for (int i = 0; i < 8; ++i) {
        Backend* b = balancer.select(backends, "");
        REQUIRE(b != nullptr);
        selection_counts[b->host]++;
    }

    // backend0 should be selected 6 times (3/4 * 8)
    // backend1 should be selected 2 times (1/4 * 8)
    int backend0_count = selection_counts["backend0"];
    int backend1_count = selection_counts["backend1"];
    REQUIRE(backend0_count == 6);
    REQUIRE(backend1_count == 2);
}

TEST_CASE("Upstream stats", "[gateway][upstream]") {
    Upstream upstream("test_upstream");

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;
    backend.status = BackendStatus::Healthy;
    upstream.add_backend(std::move(backend));

    auto stats = upstream.get_stats();
    REQUIRE(stats.name == "test_upstream");
    REQUIRE(stats.total_backends == 1);
    REQUIRE(stats.healthy_backends == 1);
}

TEST_CASE("UpstreamManager", "[gateway][upstream]") {
    UpstreamManager manager;

    auto upstream1 = std::make_unique<Upstream>("api");
    auto upstream2 = std::make_unique<Upstream>("web");

    manager.register_upstream(std::move(upstream1));
    manager.register_upstream(std::move(upstream2));

    SECTION("Get upstream by name") {
        Upstream* api = manager.get_upstream("api");
        REQUIRE(api != nullptr);
        REQUIRE(api->name() == "api");

        Upstream* web = manager.get_upstream("web");
        REQUIRE(web != nullptr);
        REQUIRE(web->name() == "web");

        Upstream* missing = manager.get_upstream("missing");
        REQUIRE(missing == nullptr);
    }

    SECTION("Remove upstream") {
        manager.remove_upstream("api");
        REQUIRE(manager.get_upstream("api") == nullptr);
        REQUIRE(manager.get_upstream("web") != nullptr);
    }
}

TEST_CASE("Pipeline execution", "[gateway][pipeline]") {
    Pipeline pipeline;

    bool middleware1_called = false;
    bool middleware2_called = false;

    pipeline.use([&](RequestContext& ctx) {
        middleware1_called = true;
        return MiddlewareResult::Continue;
    }, "middleware1");

    pipeline.use([&](RequestContext& ctx) {
        middleware2_called = true;
        return MiddlewareResult::Continue;
    }, "middleware2");

    RequestContext ctx;
    auto result = pipeline.execute(ctx);

    REQUIRE(result == MiddlewareResult::Continue);
    REQUIRE(middleware1_called);
    REQUIRE(middleware2_called);
}

TEST_CASE("Pipeline early stop", "[gateway][pipeline]") {
    Pipeline pipeline;

    bool middleware1_called = false;
    bool middleware2_called = false;

    pipeline.use([&](RequestContext& ctx) {
        middleware1_called = true;
        return MiddlewareResult::Stop;
    }, "middleware1");

    pipeline.use([&](RequestContext& ctx) {
        middleware2_called = true;
        return MiddlewareResult::Continue;
    }, "middleware2");

    RequestContext ctx;
    auto result = pipeline.execute(ctx);

    REQUIRE(result == MiddlewareResult::Stop);
    REQUIRE(middleware1_called);
    REQUIRE_FALSE(middleware2_called);
}

TEST_CASE("RequestContext metadata", "[gateway][pipeline]") {
    RequestContext ctx;

    ctx.set_metadata("key1", "value1");
    ctx.set_metadata("key2", "value2");

    REQUIRE(ctx.get_metadata("key1") == "value1");
    REQUIRE(ctx.get_metadata("key2") == "value2");
    REQUIRE(ctx.get_metadata("missing").empty());
}

TEST_CASE("RequestContext error handling", "[gateway][pipeline]") {
    RequestContext ctx;

    REQUIRE_FALSE(ctx.has_error);

    ctx.set_error("Something went wrong");

    REQUIRE(ctx.has_error);
    REQUIRE(ctx.error_message == "Something went wrong");
}
