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


// Titan Configuration - Header
// JSON configuration schema using nlohmann/json for serialization

#pragma once

#include "../gateway/router.hpp"
#include "../gateway/upstream.hpp"
#include "../http/http.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace titan::control {

/// Global server configuration
struct ServerConfig {
    uint32_t worker_threads = 0;          // 0 = auto-detect CPU count

    // Network settings
    std::string listen_address = "0.0.0.0";
    uint16_t listen_port = 8080;
    uint32_t backlog = 128;

    // Timeouts (milliseconds)
    uint32_t read_timeout = 60000;        // 60 seconds
    uint32_t write_timeout = 60000;
    uint32_t idle_timeout = 300000;       // 5 minutes
    uint32_t shutdown_timeout = 30000;    // 30 seconds

    // Limits
    uint32_t max_connections = 10000;
    uint32_t max_request_size = 1048576;  // 1MB
    uint32_t max_header_size = 8192;      // 8KB

    // TLS settings
    bool tls_enabled = false;
    std::string tls_certificate_path;     // Path to certificate file (PEM format)
    std::string tls_private_key_path;     // Path to private key file (PEM format)
    std::vector<std::string> tls_alpn_protocols = {"h2", "http/1.1"};  // ALPN protocol list
};

/// Backend server configuration
struct BackendConfig {
    std::string host;
    uint16_t port = 80;
    uint32_t weight = 1;
    uint32_t max_connections = 1000;

    // Health check settings
    bool health_check_enabled = true;
    uint32_t health_check_interval = 30;  // seconds
    uint32_t health_check_timeout = 5;    // seconds
    std::string health_check_path = "/health";
};

/// Circuit breaker configuration
struct CircuitBreakerConfigSchema {
    bool enabled = true;
    uint32_t failure_threshold = 5;      // Failures to open circuit
    uint32_t success_threshold = 2;       // Successes to close circuit
    uint32_t timeout_ms = 30000;          // Time before OPEN → HALF_OPEN (30s)
    uint32_t window_ms = 10000;           // Sliding window for failures (10s)
    bool enable_global_hints = true;      // Cross-worker catastrophic failure hints
    uint32_t catastrophic_threshold = 20; // Failures to trigger global hint
};

/// Upstream group configuration
struct UpstreamConfig {
    std::string name;
    std::vector<BackendConfig> backends;
    std::string load_balancing = "round_robin"; // round_robin, least_connections, random

    // Retry settings
    uint32_t max_retries = 2;
    uint32_t retry_timeout = 1000;        // milliseconds

    // Connection pool settings
    uint32_t pool_size = 100;
    uint32_t pool_idle_timeout = 60;      // seconds

    // Circuit breaker settings
    CircuitBreakerConfigSchema circuit_breaker;
};

/// Route configuration
struct RouteConfig {
    std::string path;
    std::string method = "GET";           // GET, POST, PUT, DELETE, etc. (empty = any)
    std::string upstream;                 // Upstream name
    std::string handler_id;               // Optional handler identifier
    uint32_t priority = 0;

    // Path rewriting
    std::optional<std::string> rewrite_path;

    // Timeout override
    std::optional<uint32_t> timeout;

    // Middleware overrides
    std::vector<std::string> middleware;
};

/// CORS middleware configuration
struct CorsConfig {
    bool enabled = false;
    std::vector<std::string> allowed_origins = {"*"};
    std::vector<std::string> allowed_methods = {"GET", "POST", "PUT", "DELETE", "OPTIONS"};
    std::vector<std::string> allowed_headers = {"*"};
    bool allow_credentials = false;
    uint32_t max_age = 86400;
};

/// Rate limiting configuration
struct RateLimitConfig {
    bool enabled = false;
    uint32_t requests_per_second = 100;
    uint32_t burst_size = 200;
    std::string key = "client_ip";        // client_ip, header:X-API-Key, etc.
};

/// Authentication configuration
struct AuthConfig {
    bool enabled = false;
    std::string type = "bearer";          // bearer, basic, apikey
    std::string header = "Authorization";
    std::vector<std::string> valid_tokens;
};

/// Logging configuration
struct LogConfig {
    std::string level = "info";           // debug, info, warning, error
    std::string format = "json";          // json, text
    bool log_requests = true;
    bool log_responses = false;
    std::vector<std::string> exclude_paths; // Don't log these paths
};

/// Metrics configuration
struct MetricsConfig {
    bool enabled = true;
    uint16_t port = 9090;
    std::string path = "/metrics";
    std::string format = "prometheus";
};

/// Full Titan configuration
struct Config {
    ServerConfig server;
    std::vector<RouteConfig> routes;
    std::vector<UpstreamConfig> upstreams;

    // Middleware configurations
    CorsConfig cors;
    RateLimitConfig rate_limit;
    AuthConfig auth;

    // Observability
    LogConfig logging;
    MetricsConfig metrics;

    // Metadata
    std::string version = "1.0";
    std::optional<std::string> description;
};

// nlohmann/json serialization macros
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ServerConfig, worker_threads, listen_address, listen_port, backlog,
    read_timeout, write_timeout, idle_timeout, shutdown_timeout, max_connections, max_request_size,
    max_header_size, tls_enabled, tls_certificate_path, tls_private_key_path, tls_alpn_protocols);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BackendConfig, host, port, weight, max_connections,
    health_check_enabled, health_check_interval, health_check_timeout, health_check_path);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CircuitBreakerConfigSchema, enabled, failure_threshold,
    success_threshold, timeout_ms, window_ms, enable_global_hints, catastrophic_threshold);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UpstreamConfig, name, backends, load_balancing, max_retries,
    retry_timeout, pool_size, pool_idle_timeout, circuit_breaker);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RouteConfig, path, method, upstream, handler_id, priority,
    rewrite_path, timeout, middleware);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CorsConfig, enabled, allowed_origins, allowed_methods,
    allowed_headers, allow_credentials, max_age);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RateLimitConfig, enabled, requests_per_second, burst_size, key);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthConfig, enabled, type, header, valid_tokens);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogConfig, level, format, log_requests, log_responses, exclude_paths);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MetricsConfig, enabled, port, path, format);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Config, server, routes, upstreams, cors, rate_limit, auth,
    logging, metrics, version, description);

/// Configuration validation result
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void add_error(std::string error) {
        valid = false;
        errors.push_back(std::move(error));
    }

    void add_warning(std::string warning) {
        warnings.push_back(std::move(warning));
    }

    [[nodiscard]] bool has_errors() const noexcept {
        return !valid || !errors.empty();
    }
};

/// Configuration loader
class ConfigLoader {
public:
    /// Load configuration from JSON file
    [[nodiscard]] static std::optional<Config> load_from_file(std::string_view path);

    /// Load configuration from JSON string
    [[nodiscard]] static std::optional<Config> load_from_json(std::string_view json);

    /// Validate configuration
    [[nodiscard]] static ValidationResult validate(const Config& config);

    /// Save configuration to JSON file
    [[nodiscard]] static bool save_to_file(const Config& config, std::string_view path);

    /// Convert configuration to JSON string
    [[nodiscard]] static std::string to_json(const Config& config);
};

/// Configuration manager with hot-reload support (RCU pattern)
class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // Non-copyable, non-movable
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /// Load initial configuration
    [[nodiscard]] bool load(std::string_view path);

    /// Reload configuration (hot-reload with RCU)
    [[nodiscard]] bool reload();

    /// Get current configuration (thread-safe read)
    [[nodiscard]] std::shared_ptr<const Config> get() const noexcept;

    /// Get configuration file path
    [[nodiscard]] std::string_view config_path() const noexcept {
        return config_path_;
    }

    /// Check if configuration is loaded
    [[nodiscard]] bool is_loaded() const noexcept {
        return current_config_ != nullptr;
    }

    /// Get last validation result
    [[nodiscard]] const ValidationResult& last_validation() const noexcept {
        return last_validation_;
    }

private:
    std::string config_path_;
    std::shared_ptr<const Config> current_config_;
    ValidationResult last_validation_;
};

} // namespace titan::control
