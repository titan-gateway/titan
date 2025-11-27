// Titan Load Balancing & Backend Unit Tests

#include "../../src/gateway/upstream.hpp"
#include "../../src/http/http.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace titan::gateway;
using namespace titan::http;

// ============================================================================
// Load Balancer Tests
// ============================================================================

TEST_CASE("RoundRobinBalancer - Distributes evenly", "[keepalive][loadbalancer]") {
    RoundRobinBalancer balancer;

    std::vector<Backend> backends;
    for (int i = 0; i < 3; i++) {
        Backend b;
        b.host = "backend" + std::to_string(i);
        b.port = 3000 + i;
        b.max_connections = 100;
        backends.push_back(std::move(b));
    }

    // Select 9 times - should get each backend 3 times
    std::vector<int> counts(3, 0);
    for (int i = 0; i < 9; i++) {
        Backend* selected = balancer.select(backends, "");
        REQUIRE(selected != nullptr);

        // Find which backend was selected
        for (size_t j = 0; j < backends.size(); j++) {
            if (selected == &backends[j]) {
                counts[j]++;
                break;
            }
        }
    }

    REQUIRE(counts[0] == 3);
    REQUIRE(counts[1] == 3);
    REQUIRE(counts[2] == 3);
}

TEST_CASE("LeastConnectionsBalancer - Selects least loaded", "[keepalive][loadbalancer]") {
    LeastConnectionsBalancer balancer;

    std::vector<Backend> backends;
    for (int i = 0; i < 3; i++) {
        Backend b;
        b.host = "backend" + std::to_string(i);
        b.port = 3000 + i;
        b.max_connections = 100;
        b.active_connections = i * 10; // 0, 10, 20
        backends.push_back(std::move(b));
    }

    Backend* selected = balancer.select(backends, "");

    REQUIRE(selected != nullptr);
    REQUIRE(selected->active_connections == 0); // First backend has least
}

// ============================================================================
// Backend Tests
// ============================================================================

TEST_CASE("Backend - can_accept_connection respects max", "[keepalive][backend]") {
    Backend backend;
    backend.host = "test";
    backend.port = 8080;
    backend.max_connections = 10;
    backend.active_connections = 5;

    REQUIRE(backend.can_accept_connection());

    backend.active_connections = 10;
    REQUIRE_FALSE(backend.can_accept_connection());
}

TEST_CASE("Backend - is_available checks status", "[keepalive][backend]") {
    Backend backend;
    backend.host = "test";
    backend.port = 8080;

    backend.status = BackendStatus::Healthy;
    REQUIRE(backend.is_available());

    backend.status = BackendStatus::Degraded;
    REQUIRE(backend.is_available());

    backend.status = BackendStatus::Unhealthy;
    REQUIRE_FALSE(backend.is_available());

    backend.status = BackendStatus::Draining;
    REQUIRE_FALSE(backend.is_available());
}

TEST_CASE("Backend - address formatting", "[keepalive][backend]") {
    Backend backend;
    backend.host = "example.com";
    backend.port = 8080;

    REQUIRE(backend.address() == "example.com:8080");
}
