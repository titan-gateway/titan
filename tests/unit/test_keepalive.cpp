// Titan Connection Pooling & Keep-Alive Unit Tests

#include "../../src/gateway/upstream.hpp"
#include "../../src/http/http.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>

using namespace titan::gateway;
using namespace titan::http;

// ============================================================================
// Connection Pool Tests
// ============================================================================

TEST_CASE("ConnectionPool - Acquire creates new connection", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    Connection* conn = pool.acquire(&backend);

    REQUIRE(conn != nullptr);
    REQUIRE(conn->backend == &backend);
    REQUIRE_FALSE(conn->is_valid()); // No socket fd assigned yet

    auto stats = pool.get_stats();
    REQUIRE(stats.total_connections == 1);
    REQUIRE(stats.active_connections == 1);
    REQUIRE(stats.cache_misses == 1);
    REQUIRE(stats.cache_hits == 0);
}

TEST_CASE("ConnectionPool - Release and reuse connection", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    // First acquire
    Connection* conn1 = pool.acquire(&backend);
    conn1->sockfd = 42; // Simulate valid socket
    conn1->keep_alive = true;

    // Release back to pool
    pool.release(conn1);

    auto stats = pool.get_stats();
    REQUIRE(stats.idle_connections == 1);
    REQUIRE(stats.active_connections == 0);

    // Second acquire - should reuse same connection
    Connection* conn2 = pool.acquire(&backend);

    REQUIRE(conn2 == conn1); // Same pointer
    REQUIRE(conn2->sockfd == 42);

    stats = pool.get_stats();
    REQUIRE(stats.cache_hits == 1);
    REQUIRE(stats.active_connections == 1);
}

TEST_CASE("ConnectionPool - Invalid connection is not reused", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    Connection* conn1 = pool.acquire(&backend);
    conn1->sockfd = -1; // Invalid socket
    conn1->keep_alive = true;

    pool.release(conn1);

    // Should create new connection since first one is invalid
    Connection* conn2 = pool.acquire(&backend);

    auto stats = pool.get_stats();
    REQUIRE(stats.cache_misses == 2); // Both were misses
}

TEST_CASE("ConnectionPool - keep_alive=false closes connection", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    Connection* conn = pool.acquire(&backend);
    conn->sockfd = 42;
    conn->keep_alive = false; // Disable keep-alive

    pool.release(conn);

    auto stats = pool.get_stats();
    REQUIRE(stats.idle_connections == 0); // Not added to free list
    REQUIRE(conn->sockfd == -1); // Socket closed
}

TEST_CASE("ConnectionPool - Expired connections are evicted", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    Connection* conn = pool.acquire(&backend);
    conn->sockfd = 42;
    conn->keep_alive = true;

    pool.release(conn);

    // Manually set last_used to 2 hours ago (after release updated it to now)
    conn->last_used = std::chrono::steady_clock::now() - std::chrono::hours(2);

    // Evict connections idle for > 1 hour
    pool.evict_expired(std::chrono::hours(1));

    auto stats = pool.get_stats();
    REQUIRE(stats.idle_connections == 0);
}

TEST_CASE("ConnectionPool - Old connections are closed", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    Connection* conn = pool.acquire(&backend);
    conn->sockfd = 42;
    conn->keep_alive = true;
    conn->created_at = std::chrono::steady_clock::now() - std::chrono::hours(2);

    pool.release(conn);

    auto stats = pool.get_stats();
    REQUIRE(stats.idle_connections == 0); // Closed due to max lifetime
}

TEST_CASE("ConnectionPool - LIFO order (cache locality)", "[keepalive][pool]") {
    ConnectionPool pool(10);

    Backend backend;
    backend.host = "localhost";
    backend.port = 8080;

    // Create and release 3 connections
    Connection* conn1 = pool.acquire(&backend);
    conn1->sockfd = 1;
    conn1->keep_alive = true;
    pool.release(conn1);

    Connection* conn2 = pool.acquire(&backend);
    conn2->sockfd = 2;
    conn2->keep_alive = true;
    pool.release(conn2);

    Connection* conn3 = pool.acquire(&backend);
    conn3->sockfd = 3;
    conn3->keep_alive = true;
    pool.release(conn3);

    // Next acquire should get conn3 (LIFO - last in, first out)
    Connection* next = pool.acquire(&backend);
    REQUIRE(next->sockfd == 3);
}

// ============================================================================
// Upstream & Load Balancer Tests
// ============================================================================

TEST_CASE("Upstream - Get connection from pool", "[keepalive][upstream]") {
    Upstream upstream("test");

    Backend backend;
    backend.host = "backend1";
    backend.port = 3000;
    backend.max_connections = 100;

    upstream.add_backend(std::move(backend));

    Connection* conn = upstream.get_connection("127.0.0.1");

    REQUIRE(conn != nullptr);
    REQUIRE(conn->backend != nullptr);
    REQUIRE(conn->backend->host == "backend1");

    auto stats = upstream.get_stats();
    REQUIRE(stats.pool_stats.active_connections == 1);
}

TEST_CASE("Upstream - Release connection back to pool", "[keepalive][upstream]") {
    Upstream upstream("test");

    Backend backend;
    backend.host = "backend1";
    backend.port = 3000;

    upstream.add_backend(std::move(backend));

    Connection* conn = upstream.get_connection("127.0.0.1");
    conn->sockfd = 42;
    conn->keep_alive = true;

    upstream.release_connection(conn);

    auto stats = upstream.get_stats();
    REQUIRE(stats.pool_stats.idle_connections == 1);

    // Get again - should reuse
    Connection* conn2 = upstream.get_connection("127.0.0.1");
    REQUIRE(conn2 == conn);

    // Refresh stats after second get_connection
    stats = upstream.get_stats();
    REQUIRE(stats.pool_stats.cache_hits == 1);
}

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

// ============================================================================
// Connection Tests
// ============================================================================

TEST_CASE("Connection - is_valid checks socket and backend", "[keepalive][connection]") {
    Connection conn;

    REQUIRE_FALSE(conn.is_valid());

    conn.sockfd = 42;
    REQUIRE_FALSE(conn.is_valid()); // Still need backend

    Backend backend;
    conn.backend = &backend;
    REQUIRE(conn.is_valid()); // Now valid

    conn.sockfd = -1;
    REQUIRE_FALSE(conn.is_valid()); // Invalid socket
}

TEST_CASE("Connection - is_expired checks idle time", "[keepalive][connection]") {
    Connection conn;
    conn.last_used = std::chrono::steady_clock::now() - std::chrono::minutes(5);

    REQUIRE_FALSE(conn.is_expired(std::chrono::minutes(10)));
    REQUIRE(conn.is_expired(std::chrono::minutes(3)));
}

TEST_CASE("Connection - is_old checks lifetime", "[keepalive][connection]") {
    Connection conn;
    conn.created_at = std::chrono::steady_clock::now() - std::chrono::hours(2);

    REQUIRE_FALSE(conn.is_old(std::chrono::hours(3)));
    REQUIRE(conn.is_old(std::chrono::hours(1)));
}
