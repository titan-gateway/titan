// Titan Configuration Layer Unit Tests

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "../../src/control/config.hpp"

using namespace titan::control;

TEST_CASE("Config JSON serialization", "[control][config]") {
    Config config;
    config.version = "1.0";
    config.server.listen_port = 8080;
    config.server.worker_threads = 4;

    std::string json = ConfigLoader::to_json(config);
    REQUIRE_FALSE(json.empty());
    REQUIRE(json.find("\"version\"") != std::string::npos);
    // nlohmann/json adds space after colon, so check for both "listen_port": 8080 or
    // "listen_port":8080
    REQUIRE((json.find("\"listen_port\": 8080") != std::string::npos ||
             json.find("\"listen_port\":8080") != std::string::npos));
}

TEST_CASE("Config JSON deserialization", "[control][config]") {
    const char* json = R"({
        "version": "1.0",
        "server": {
            "worker_threads": 2,
            "listen_port": 9000
        },
        "upstreams": [],
        "routes": []
    })";

    auto maybe_config = ConfigLoader::load_from_json(json);
    REQUIRE(maybe_config.has_value());

    const auto& config = *maybe_config;
    REQUIRE(config.version == "1.0");
    REQUIRE(config.server.worker_threads == 2);
    REQUIRE(config.server.listen_port == 9000);
}

TEST_CASE("Config validation - valid config", "[control][config]") {
    Config config;
    config.version = "1.0";
    config.server.listen_port = 8080;

    UpstreamConfig upstream;
    upstream.name = "test_upstream";
    BackendConfig backend;
    backend.host = "localhost";
    backend.port = 3000;
    upstream.backends.push_back(backend);
    config.upstreams.push_back(upstream);

    RouteConfig route;
    route.path = "/test";
    route.method = "GET";
    route.upstream = "test_upstream";
    config.routes.push_back(route);

    auto validation = ConfigLoader::validate(config);
    REQUIRE(validation.valid);
    REQUIRE_FALSE(validation.has_errors());
}

TEST_CASE("Config validation - missing upstream", "[control][config]") {
    Config config;
    config.server.listen_port = 8080;

    RouteConfig route;
    route.path = "/test";
    route.method = "GET";
    route.upstream = "nonexistent";
    config.routes.push_back(route);

    auto validation = ConfigLoader::validate(config);
    REQUIRE_FALSE(validation.valid);
    REQUIRE(validation.has_errors());
    REQUIRE(validation.errors.size() > 0);
}

TEST_CASE("Config validation - empty backend host", "[control][config]") {
    Config config;
    config.server.listen_port = 8080;

    UpstreamConfig upstream;
    upstream.name = "test";
    BackendConfig backend;
    backend.host = "";  // Empty host
    backend.port = 3000;
    upstream.backends.push_back(backend);
    config.upstreams.push_back(upstream);

    auto validation = ConfigLoader::validate(config);
    REQUIRE_FALSE(validation.valid);
    REQUIRE(validation.has_errors());
}

TEST_CASE("Config validation - invalid listen port", "[control][config]") {
    Config config;
    config.server.listen_port = 0;  // Invalid port

    auto validation = ConfigLoader::validate(config);
    REQUIRE_FALSE(validation.valid);
    REQUIRE(validation.has_errors());
}

TEST_CASE("Config validation - invalid HTTP method", "[control][config]") {
    Config config;
    config.server.listen_port = 8080;

    UpstreamConfig upstream;
    upstream.name = "test";
    BackendConfig backend;
    backend.host = "localhost";
    backend.port = 3000;
    upstream.backends.push_back(backend);
    config.upstreams.push_back(upstream);

    RouteConfig route;
    route.path = "/test";
    route.method = "INVALID_METHOD";
    route.upstream = "test";
    config.routes.push_back(route);

    auto validation = ConfigLoader::validate(config);
    REQUIRE_FALSE(validation.valid);
    REQUIRE(validation.has_errors());
}

TEST_CASE("Config validation - invalid load balancing strategy", "[control][config]") {
    Config config;
    config.server.listen_port = 8080;

    UpstreamConfig upstream;
    upstream.name = "test";
    upstream.load_balancing = "invalid_strategy";
    BackendConfig backend;
    backend.host = "localhost";
    backend.port = 3000;
    upstream.backends.push_back(backend);
    config.upstreams.push_back(upstream);

    auto validation = ConfigLoader::validate(config);
    REQUIRE_FALSE(validation.valid);
    REQUIRE(validation.has_errors());
}

TEST_CASE("Config file operations", "[control][config]") {
    Config config;
    config.version = "test";
    config.server.listen_port = 7777;

    // Write to temporary file
    std::string temp_path = "/tmp/titan_test_config.json";
    bool saved = ConfigLoader::save_to_file(config, temp_path);
    REQUIRE(saved);

    // Read back
    auto maybe_config = ConfigLoader::load_from_file(temp_path);
    REQUIRE(maybe_config.has_value());
    REQUIRE(maybe_config->version == "test");
    REQUIRE(maybe_config->server.listen_port == 7777);

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST_CASE("ConfigManager load and get", "[control][config]") {
    // Create temporary config file
    const char* json = R"({
        "version": "manager_test",
        "server": {"listen_port": 8888},
        "upstreams": [
            {
                "name": "test_upstream",
                "load_balancing": "round_robin",
                "backends": [{"host": "localhost", "port": 3000}]
            }
        ],
        "routes": [
            {"path": "/", "method": "GET", "upstream": "test_upstream"}
        ]
    })";

    std::string temp_path = "/tmp/titan_manager_test.json";
    std::ofstream file(temp_path);
    file << json;
    file.close();

    ConfigManager manager;
    bool loaded = manager.load(temp_path);
    REQUIRE(loaded);
    REQUIRE(manager.is_loaded());

    auto config = manager.get();
    REQUIRE(config != nullptr);
    REQUIRE(config->version == "manager_test");
    REQUIRE(config->server.listen_port == 8888);

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST_CASE("ConfigManager hot-reload", "[control][config]") {
    // Create initial config
    const char* initial_json = R"({
        "version": "v1",
        "server": {"listen_port": 8080},
        "upstreams": [{"name": "test", "load_balancing": "round_robin", "backends": [{"host": "localhost", "port": 3000}]}],
        "routes": [{"path": "/", "method": "GET", "upstream": "test"}]
    })";

    std::string temp_path = "/tmp/titan_reload_test.json";
    std::ofstream file1(temp_path);
    file1 << initial_json;
    file1.close();

    ConfigManager manager;
    manager.load(temp_path);

    auto config1 = manager.get();
    REQUIRE(config1->version == "v1");

    // Update config file
    const char* updated_json = R"({
        "version": "v2",
        "server": {"listen_port": 9090},
        "upstreams": [{"name": "test", "load_balancing": "least_connections", "backends": [{"host": "localhost", "port": 3001}]}],
        "routes": [{"path": "/new", "method": "POST", "upstream": "test"}]
    })";

    std::ofstream file2(temp_path);
    file2 << updated_json;
    file2.close();

    // Reload
    bool reloaded = manager.reload();
    REQUIRE(reloaded);

    auto config2 = manager.get();
    REQUIRE(config2->version == "v2");
    REQUIRE(config2->server.listen_port == 9090);

    // Old config should still be valid (RCU pattern)
    REQUIRE(config1->version == "v1");

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST_CASE("ConfigManager RCU safety", "[control][config]") {
    const char* json = R"({
        "version": "rcu_test",
        "server": {"listen_port": 8080},
        "upstreams": [{"name": "test", "load_balancing": "round_robin", "backends": [{"host": "localhost", "port": 3000}]}],
        "routes": [{"path": "/", "method": "GET", "upstream": "test"}]
    })";

    std::string temp_path = "/tmp/titan_rcu_test.json";
    std::ofstream file(temp_path);
    file << json;
    file.close();

    ConfigManager manager;
    manager.load(temp_path);

    // Get multiple references to config
    auto ref1 = manager.get();
    auto ref2 = manager.get();
    auto ref3 = manager.get();

    // All references should point to same config
    REQUIRE(ref1.get() == ref2.get());
    REQUIRE(ref2.get() == ref3.get());
    REQUIRE(ref1->version == "rcu_test");

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST_CASE("Config with all fields", "[control][config]") {
    const char* json = R"({
        "version": "1.0",
        "description": "Test config",
        "server": {
            "worker_threads": 4,
            "listen_address": "127.0.0.1",
            "listen_port": 8080,
            "backlog": 256,
            "read_timeout": 30000,
            "write_timeout": 30000,
            "idle_timeout": 120000,
            "shutdown_timeout": 10000,
            "max_connections": 5000,
            "max_request_size": 2097152,
            "max_header_size": 16384
        },
        "upstreams": [
            {
                "name": "api",
                "load_balancing": "least_connections",
                "max_retries": 3,
                "retry_timeout": 2000,
                "pool_size": 200,
                "pool_idle_timeout": 90,
                "backends": [
                    {
                        "host": "api.example.com",
                        "port": 443,
                        "weight": 2,
                        "max_connections": 2000,
                        "health_check_enabled": true,
                        "health_check_interval": 15,
                        "health_check_timeout": 3,
                        "health_check_path": "/api/health"
                    }
                ]
            }
        ],
        "routes": [
            {
                "path": "/api/*",
                "method": "GET",
                "upstream": "api",
                "handler_id": "api_handler",
                "priority": 100
            }
        ],
        "cors": {
            "enabled": true,
            "allowed_origins": ["https://example.com"],
            "allowed_methods": ["GET", "POST"],
            "allowed_headers": ["Content-Type"],
            "allow_credentials": true,
            "max_age": 3600
        },
        "rate_limit": {
            "enabled": true,
            "requests_per_second": 500,
            "burst_size": 1000,
            "key": "client_ip"
        },
        "auth": {
            "enabled": true,
            "type": "bearer",
            "header": "Authorization",
            "valid_tokens": ["token1", "token2"]
        },
        "logging": {
            "level": "debug",
            "format": "text",
            "log_requests": true,
            "log_responses": true,
            "exclude_paths": ["/health"]
        },
        "metrics": {
            "enabled": true,
            "port": 9091,
            "path": "/prometheus",
            "format": "prometheus"
        }
    })";

    auto maybe_config = ConfigLoader::load_from_json(json);
    REQUIRE(maybe_config.has_value());

    const auto& config = *maybe_config;
    REQUIRE(config.version == "1.0");
    REQUIRE(config.description == "Test config");
    REQUIRE(config.server.worker_threads == 4);
    REQUIRE(config.upstreams.size() == 1);
    REQUIRE(config.upstreams[0].name == "api");
    REQUIRE(config.upstreams[0].load_balancing == "least_connections");
    REQUIRE(config.routes.size() == 1);
    REQUIRE(config.routes[0].path == "/api/*");
    REQUIRE(config.cors.enabled);
    REQUIRE(config.cors.allow_credentials);
    REQUIRE(config.rate_limit.enabled);
    REQUIRE(config.auth.enabled);
    REQUIRE(config.auth.valid_tokens.size() == 2);
    REQUIRE(config.logging.level == "debug");
    REQUIRE(config.metrics.enabled);
}
