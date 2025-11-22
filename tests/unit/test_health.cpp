// Health Check Tests

#include "control/health.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace titan::control;

TEST_CASE("HealthChecker initialization", "[control][health]") {
    HealthChecker checker;

    SECTION("Initial state") {
        REQUIRE_FALSE(checker.is_running());

        auto health = checker.get_server_health();
        REQUIRE(health.status == HealthStatus::Healthy);
        REQUIRE(health.total_requests == 0);
        REQUIRE(health.total_errors == 0);
        REQUIRE(health.active_connections == 0);
        REQUIRE(health.upstreams.empty());
    }

    SECTION("Start and stop") {
        checker.start();
        REQUIRE(checker.is_running());

        checker.stop();
        REQUIRE_FALSE(checker.is_running());
    }
}

TEST_CASE("HealthChecker metrics recording", "[control][health]") {
    HealthChecker checker;
    checker.start();

    SECTION("Record requests") {
        checker.record_request();
        checker.record_request();
        checker.record_request();

        auto health = checker.get_server_health();
        REQUIRE(health.total_requests == 3);
        REQUIRE(health.total_errors == 0);
        REQUIRE(health.status == HealthStatus::Healthy);
    }

    SECTION("Record errors") {
        checker.record_request();
        checker.record_request();
        checker.record_error();

        auto health = checker.get_server_health();
        REQUIRE(health.total_requests == 2);
        REQUIRE(health.total_errors == 1);

        // Error rate = 1/2 = 50% -> Unhealthy
        REQUIRE(health.status == HealthStatus::Unhealthy);
    }

    SECTION("Degraded status") {
        // Error rate between 10% and 50% should be degraded
        for (int i = 0; i < 10; ++i) {
            checker.record_request();
        }
        checker.record_error();  // 1 error out of 10 = 10% -> Still healthy
        checker.record_error();  // 2 errors out of 10 = 20% -> Degraded

        auto health = checker.get_server_health();
        REQUIRE(health.total_requests == 10);
        REQUIRE(health.total_errors == 2);
        REQUIRE(health.status == HealthStatus::Degraded);
    }

    SECTION("Active connections") {
        checker.update_active_connections(42);

        auto health = checker.get_server_health();
        REQUIRE(health.active_connections == 42);
    }
}

TEST_CASE("Backend health tracking", "[control][health]") {
    HealthChecker checker;
    checker.start();

    SECTION("Update backend health") {
        checker.update_backend_health("api_backend", "localhost", 3000, HealthStatus::Healthy);

        auto health = checker.get_server_health();
        REQUIRE(health.upstreams.size() == 1);
        REQUIRE(health.upstreams[0].name == "api_backend");
        REQUIRE(health.upstreams[0].healthy_backends == 1);
        REQUIRE(health.upstreams[0].total_backends == 1);
        REQUIRE(health.upstreams[0].status == HealthStatus::Healthy);

        REQUIRE(health.upstreams[0].backends.size() == 1);
        REQUIRE(health.upstreams[0].backends[0].host == "localhost");
        REQUIRE(health.upstreams[0].backends[0].port == 3000);
        REQUIRE(health.upstreams[0].backends[0].status == HealthStatus::Healthy);
    }

    SECTION("Multiple backends") {
        checker.update_backend_health("api_backend", "localhost", 3000, HealthStatus::Healthy);
        checker.update_backend_health("api_backend", "localhost", 3001, HealthStatus::Healthy);
        checker.update_backend_health("api_backend", "localhost", 3002, HealthStatus::Unhealthy);

        auto health = checker.get_server_health();
        REQUIRE(health.upstreams.size() == 1);
        REQUIRE(health.upstreams[0].healthy_backends == 2);
        REQUIRE(health.upstreams[0].total_backends == 3);
        REQUIRE(health.upstreams[0].status == HealthStatus::Degraded);  // Not all healthy
    }

    SECTION("Multiple upstreams") {
        checker.update_backend_health("api_backend", "localhost", 3000, HealthStatus::Healthy);
        checker.update_backend_health("web_backend", "localhost", 8000, HealthStatus::Healthy);

        auto health = checker.get_server_health();
        REQUIRE(health.upstreams.size() == 2);

        // Find upstreams by name (order not guaranteed)
        const UpstreamHealth* api = nullptr;
        const UpstreamHealth* web = nullptr;

        for (const auto& upstream : health.upstreams) {
            if (upstream.name == "api_backend") {
                api = &upstream;
            } else if (upstream.name == "web_backend") {
                web = &upstream;
            }
        }

        REQUIRE(api != nullptr);
        REQUIRE(web != nullptr);
        REQUIRE(api->healthy_backends == 1);
        REQUIRE(web->healthy_backends == 1);
    }

    SECTION("All backends unhealthy") {
        checker.update_backend_health("api_backend", "localhost", 3000, HealthStatus::Unhealthy);
        checker.update_backend_health("api_backend", "localhost", 3001, HealthStatus::Unhealthy);

        auto health = checker.get_server_health();
        REQUIRE(health.upstreams.size() == 1);
        REQUIRE(health.upstreams[0].healthy_backends == 0);
        REQUIRE(health.upstreams[0].total_backends == 2);
        REQUIRE(health.upstreams[0].status == HealthStatus::Unhealthy);
    }
}

TEST_CASE("Backend health check", "[control][health]") {
    HealthChecker checker;
    checker.start();

    SECTION("Check backend") {
        auto backend_health = checker.check_backend("localhost", 3000, "/health");

        REQUIRE(backend_health.host == "localhost");
        REQUIRE(backend_health.port == 3000);
        // For MVP, check always succeeds
        REQUIRE(backend_health.status == HealthStatus::Healthy);
    }
}

TEST_CASE("HealthResponse JSON formatting", "[control][health]") {
    ServerHealth health;
    health.status = HealthStatus::Healthy;
    health.uptime = std::chrono::seconds(3600);
    health.total_requests = 1000;
    health.active_connections = 50;
    health.total_errors = 10;

    UpstreamHealth upstream;
    upstream.name = "test_upstream";
    upstream.status = HealthStatus::Healthy;
    upstream.healthy_backends = 2;
    upstream.total_backends = 2;

    BackendHealth backend1;
    backend1.host = "localhost";
    backend1.port = 3000;
    backend1.status = HealthStatus::Healthy;
    backend1.successful_checks = 100;
    backend1.failed_checks = 0;
    backend1.last_check_latency = std::chrono::milliseconds(5);

    BackendHealth backend2;
    backend2.host = "localhost";
    backend2.port = 3001;
    backend2.status = HealthStatus::Healthy;
    backend2.successful_checks = 95;
    backend2.failed_checks = 5;
    backend2.last_check_latency = std::chrono::milliseconds(8);

    upstream.backends.push_back(backend1);
    upstream.backends.push_back(backend2);
    health.upstreams.push_back(upstream);

    SECTION("JSON format") {
        std::string json = HealthResponse::to_json(health);

        REQUIRE_FALSE(json.empty());
        REQUIRE(json.find("\"status\": \"healthy\"") != std::string::npos);
        REQUIRE(json.find("\"uptime_seconds\": 3600") != std::string::npos);
        REQUIRE(json.find("\"total_requests\": 1000") != std::string::npos);
        REQUIRE(json.find("\"active_connections\": 50") != std::string::npos);
        REQUIRE(json.find("\"total_errors\": 10") != std::string::npos);
        REQUIRE(json.find("\"test_upstream\"") != std::string::npos);
        REQUIRE(json.find("\"localhost\"") != std::string::npos);
        REQUIRE(json.find("\"port\": 3000") != std::string::npos);
    }

    SECTION("Text format") {
        std::string text = HealthResponse::to_text(health);

        REQUIRE_FALSE(text.empty());
        REQUIRE(text.find("OK") != std::string::npos);
        REQUIRE(text.find("3600s") != std::string::npos);
        REQUIRE(text.find("1000") != std::string::npos);
        REQUIRE(text.find("50") != std::string::npos);
    }

    SECTION("HTTP status codes") {
        REQUIRE(HealthResponse::to_http_status(HealthStatus::Healthy) == 200);
        REQUIRE(HealthResponse::to_http_status(HealthStatus::Degraded) == 200);
        REQUIRE(HealthResponse::to_http_status(HealthStatus::Unhealthy) == 503);
    }
}

TEST_CASE("Server uptime calculation", "[control][health]") {
    HealthChecker checker;
    checker.start();

    // Simulate some time passing (we can't actually wait)
    auto health = checker.get_server_health();

    // Uptime should be very small (close to 0)
    REQUIRE(health.uptime.count() >= 0);
    REQUIRE(health.uptime.count() < 10);  // Should be less than 10 seconds
}
