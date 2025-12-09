// Titan Gateway Layer Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <map>

#include "../../src/gateway/pipeline.hpp"
#include "../../src/gateway/router.hpp"
#include "../../src/gateway/upstream.hpp"

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

TEST_CASE("Router routes with common prefixes (radix tree splitting)", "[gateway][router]") {
    Router router;

    // These routes share common prefixes and require radix tree node splitting
    router.add_route(Route{"/public", Method::GET, "public"});
    router.add_route(Route{"/protected", Method::GET, "protected"});
    router.add_route(Route{"/privacy", Method::GET, "privacy"});

    SECTION("Match routes with shared prefix 'p'") {
        auto match1 = router.match(Method::GET, "/public");
        REQUIRE(match1.matched());
        REQUIRE(match1.handler_id == "public");

        auto match2 = router.match(Method::GET, "/protected");
        REQUIRE(match2.matched());
        REQUIRE(match2.handler_id == "protected");

        auto match3 = router.match(Method::GET, "/privacy");
        REQUIRE(match3.matched());
        REQUIRE(match3.handler_id == "privacy");
    }

    SECTION("More complex common prefixes") {
        Router r2;
        r2.add_route(Route{"/api/users", Method::GET, "list_users"});
        r2.add_route(Route{"/api/posts", Method::GET, "list_posts"});
        r2.add_route(Route{"/api/user/:id", Method::GET, "get_user"});

        auto match1 = r2.match(Method::GET, "/api/users");
        REQUIRE(match1.matched());
        REQUIRE(match1.handler_id == "list_users");

        auto match2 = r2.match(Method::GET, "/api/posts");
        REQUIRE(match2.matched());
        REQUIRE(match2.handler_id == "list_posts");

        auto match3 = r2.match(Method::GET, "/api/user/123");
        REQUIRE(match3.matched());
        REQUIRE(match3.handler_id == "get_user");
    }

    SECTION("Non-matching routes should return empty") {
        auto no_match = router.match(Method::GET, "/private");
        REQUIRE_FALSE(no_match.matched());
    }
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
        b.active_connections = i * 5;  // 0, 5, 10
        backends.push_back(std::move(b));
    }

    LeastConnectionsBalancer balancer;

    Backend* selected = balancer.select(backends, "");
    REQUIRE(selected != nullptr);
    REQUIRE(selected->host == "backend0");  // Has 0 connections
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

    pipeline.use(
        [&](RequestContext& ctx) {
            middleware1_called = true;
            return MiddlewareResult::Continue;
        },
        "middleware1");

    pipeline.use(
        [&](RequestContext& ctx) {
            middleware2_called = true;
            return MiddlewareResult::Continue;
        },
        "middleware2");

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

    pipeline.use(
        [&](RequestContext& ctx) {
            middleware1_called = true;
            return MiddlewareResult::Stop;
        },
        "middleware1");

    pipeline.use(
        [&](RequestContext& ctx) {
            middleware2_called = true;
            return MiddlewareResult::Continue;
        },
        "middleware2");

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

// ============================================================================
// Route Cache Tests
// ============================================================================

TEST_CASE("Router cache - Basic caching", "[gateway][router][cache]") {
    Router router;

    Route route;
    route.path = "/api/users";
    route.method = Method::GET;
    route.handler_id = "get_users";
    route.upstream_name = "backend";
    router.add_route(std::move(route));

    SECTION("First access is cache miss") {
        router.clear_cache();  // Clear thread-local cache
        auto stats_before = router.get_stats();
        REQUIRE(stats_before.cache_misses == 0);
        REQUIRE(stats_before.cache_hits == 0);

        auto match = router.match(Method::GET, "/api/users");
        REQUIRE(match.matched());
        REQUIRE(match.handler_id == "get_users");

        auto stats_after = router.get_stats();
        REQUIRE(stats_after.cache_misses == 1);
        REQUIRE(stats_after.cache_hits == 0);
        REQUIRE(stats_after.cache_size == 1);
    }

    SECTION("Second access is cache hit") {
        router.clear_cache();  // Clear thread-local cache
        // First access - miss
        auto match1 = router.match(Method::GET, "/api/users");
        REQUIRE(match1.matched());

        // Second access - hit
        auto match2 = router.match(Method::GET, "/api/users");
        REQUIRE(match2.matched());
        REQUIRE(match2.handler_id == "get_users");

        auto stats = router.get_stats();
        REQUIRE(stats.cache_hits == 1);
        REQUIRE(stats.cache_misses == 1);
        REQUIRE(stats.cache_size == 1);
    }

    SECTION("Multiple cache hits") {
        router.clear_cache();  // Clear thread-local cache
        // Warm up cache
        router.match(Method::GET, "/api/users");

        // Multiple hits
        for (int i = 0; i < 10; ++i) {
            auto match = router.match(Method::GET, "/api/users");
            REQUIRE(match.matched());
            REQUIRE(match.handler_id == "get_users");
        }

        auto stats = router.get_stats();
        REQUIRE(stats.cache_hits == 10);
        REQUIRE(stats.cache_misses == 1);
    }
}

TEST_CASE("Router cache - Method differentiation", "[gateway][router][cache]") {
    Router router;

    Route get_route;
    get_route.path = "/api/resource";
    get_route.method = Method::GET;
    get_route.handler_id = "get_resource";
    router.add_route(std::move(get_route));

    Route post_route;
    post_route.path = "/api/resource";
    post_route.method = Method::POST;
    post_route.handler_id = "create_resource";
    router.add_route(std::move(post_route));

    SECTION("Different methods are cached separately") {
        // Access GET
        auto get_match = router.match(Method::GET, "/api/resource");
        REQUIRE(get_match.matched());
        REQUIRE(get_match.handler_id == "get_resource");

        // Access POST
        auto post_match = router.match(Method::POST, "/api/resource");
        REQUIRE(post_match.matched());
        REQUIRE(post_match.handler_id == "create_resource");

        // Both should be cache misses
        auto stats = router.get_stats();
        REQUIRE(stats.cache_misses == 2);
        REQUIRE(stats.cache_size == 2);

        // Verify cache hits work for both
        router.match(Method::GET, "/api/resource");
        router.match(Method::POST, "/api/resource");

        stats = router.get_stats();
        REQUIRE(stats.cache_hits == 2);
    }
}

TEST_CASE("Router cache - Parameters preserved", "[gateway][router][cache]") {
    Router router;

    Route route;
    route.path = "/users/:id";
    route.method = Method::GET;
    route.handler_id = "get_user";
    router.add_route(std::move(route));

    SECTION("Cached route preserves parameters") {
        router.clear_cache();  // Clear thread-local cache
        // First access - cache miss
        auto match1 = router.match(Method::GET, "/users/123");
        REQUIRE(match1.matched());
        REQUIRE(match1.params.size() == 1);
        REQUIRE(match1.params[0].name == "id");
        REQUIRE(match1.params[0].value == "123");

        // Second access - cache hit
        auto match2 = router.match(Method::GET, "/users/123");
        REQUIRE(match2.matched());
        REQUIRE(match2.params.size() == 1);
        REQUIRE(match2.params[0].value == "123");

        auto stats = router.get_stats();
        REQUIRE(stats.cache_hits == 1);
    }

    SECTION("Different parameter values cached separately") {
        router.clear_cache();  // Clear thread-local cache
        router.match(Method::GET, "/users/123");
        router.match(Method::GET, "/users/456");
        router.match(Method::GET, "/users/789");

        auto stats = router.get_stats();
        REQUIRE(stats.cache_size == 3);
        REQUIRE(stats.cache_misses == 3);

        // Verify each is a cache hit
        router.match(Method::GET, "/users/123");
        router.match(Method::GET, "/users/456");
        router.match(Method::GET, "/users/789");

        stats = router.get_stats();
        REQUIRE(stats.cache_hits == 3);
    }
}

TEST_CASE("Router cache - Disable/enable", "[gateway][router][cache]") {
    Router router;

    Route route;
    route.path = "/api/test";
    route.method = Method::GET;
    route.handler_id = "test_handler";
    router.add_route(std::move(route));

    SECTION("Cache can be disabled") {
        router.clear_cache();  // Clear thread-local cache
        // Enable caching (default)
        REQUIRE(router.cache_enabled());
        router.match(Method::GET, "/api/test");
        router.match(Method::GET, "/api/test");

        auto stats1 = router.get_stats();
        REQUIRE(stats1.cache_hits == 1);

        // Disable caching
        router.set_cache_enabled(false);
        REQUIRE_FALSE(router.cache_enabled());

        // Multiple accesses with cache disabled
        router.match(Method::GET, "/api/test");
        router.match(Method::GET, "/api/test");
        router.match(Method::GET, "/api/test");

        auto stats2 = router.get_stats();
        // Stats should not change with cache disabled
        REQUIRE(stats2.cache_hits == 0);
        REQUIRE(stats2.cache_misses == 0);
    }

    SECTION("Re-enabling cache works") {
        router.clear_cache();  // Clear thread-local cache
        router.set_cache_enabled(false);
        router.match(Method::GET, "/api/test");

        router.set_cache_enabled(true);
        router.match(Method::GET, "/api/test");  // Miss
        router.match(Method::GET, "/api/test");  // Hit

        auto stats = router.get_stats();
        REQUIRE(stats.cache_hits == 1);
        REQUIRE(stats.cache_misses == 1);
    }
}

TEST_CASE("Router cache - Clear operations", "[gateway][router][cache]") {
    Router router;

    Route route;
    route.path = "/api/data";
    route.method = Method::GET;
    route.handler_id = "get_data";
    router.add_route(std::move(route));

    SECTION("clear_cache() clears statistics") {
        router.match(Method::GET, "/api/data");
        router.match(Method::GET, "/api/data");

        auto stats1 = router.get_stats();
        REQUIRE(stats1.cache_size == 1);
        REQUIRE(stats1.cache_hits == 1);

        router.clear_cache();

        auto stats2 = router.get_stats();
        REQUIRE(stats2.cache_size == 0);
        REQUIRE(stats2.cache_hits == 0);
        REQUIRE(stats2.cache_misses == 0);
    }

    SECTION("clear() clears both routes and cache") {
        router.match(Method::GET, "/api/data");

        auto stats1 = router.get_stats();
        REQUIRE(stats1.total_routes == 1);
        REQUIRE(stats1.cache_size == 1);

        router.clear();

        auto stats2 = router.get_stats();
        REQUIRE(stats2.total_routes == 0);
        REQUIRE(stats2.cache_size == 0);
    }
}

TEST_CASE("ThreadLocalRouteCache - LRU eviction", "[gateway][router][cache]") {
    ThreadLocalRouteCache cache(3);  // Small capacity for testing

    RouteMatch match1;
    match1.handler_id = "handler1";

    RouteMatch match2;
    match2.handler_id = "handler2";

    RouteMatch match3;
    match3.handler_id = "handler3";

    RouteMatch match4;
    match4.handler_id = "handler4";

    SECTION("Fills cache to capacity") {
        cache.put(Method::GET, "/route1", match1);
        cache.put(Method::GET, "/route2", match2);
        cache.put(Method::GET, "/route3", match3);

        REQUIRE(cache.size() == 3);
        REQUIRE(cache.capacity() == 3);
    }

    SECTION("LRU eviction on overflow") {
        cache.put(Method::GET, "/route1", match1);
        cache.put(Method::GET, "/route2", match2);
        cache.put(Method::GET, "/route3", match3);

        // Access route1 to make it recently used
        auto cached1 = cache.get(Method::GET, "/route1");
        REQUIRE(cached1.has_value());

        // Add route4 - should evict route2 (oldest unused)
        cache.put(Method::GET, "/route4", match4);

        REQUIRE(cache.size() == 3);

        // route1 should still be cached
        auto cached1_again = cache.get(Method::GET, "/route1");
        REQUIRE(cached1_again.has_value());

        // route4 should be cached
        auto cached4 = cache.get(Method::GET, "/route4");
        REQUIRE(cached4.has_value());

        // route2 should be evicted
        auto cached2 = cache.get(Method::GET, "/route2");
        REQUIRE_FALSE(cached2.has_value());
    }

    SECTION("Clear resets all statistics") {
        cache.put(Method::GET, "/route1", match1);
        cache.get(Method::GET, "/route1");
        cache.get(Method::GET, "/missing");

        REQUIRE(cache.hits() > 0);
        REQUIRE(cache.misses() > 0);
        REQUIRE(cache.size() > 0);

        cache.clear();

        REQUIRE(cache.hits() == 0);
        REQUIRE(cache.misses() == 0);
        REQUIRE(cache.size() == 0);
    }
}
