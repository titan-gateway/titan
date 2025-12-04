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

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "../gateway/router.hpp"
#include "../gateway/upstream.hpp"
#include "../http/http.hpp"

namespace titan::control {

/// Global server configuration
struct ServerConfig {
    uint32_t worker_threads = 0;  // 0 = auto-detect CPU count

    // Network settings
    std::string listen_address = "0.0.0.0";
    uint16_t listen_port = 8080;
    uint32_t backlog = 128;

    // Timeouts (milliseconds)
    uint32_t read_timeout = 60000;  // 60 seconds
    uint32_t write_timeout = 60000;
    uint32_t idle_timeout = 300000;     // 5 minutes
    uint32_t shutdown_timeout = 30000;  // 30 seconds

    // Limits
    uint32_t max_connections = 10000;
    uint32_t max_request_size = 1048576;  // 1MB
    uint32_t max_header_size = 8192;      // 8KB

    // TLS settings
    bool tls_enabled = false;
    std::string tls_certificate_path;  // Path to certificate file (PEM format)
    std::string tls_private_key_path;  // Path to private key file (PEM format)
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
    uint32_t failure_threshold = 5;        // Failures to open circuit
    uint32_t success_threshold = 2;        // Successes to close circuit
    uint32_t timeout_ms = 30000;           // Time before OPEN → HALF_OPEN (30s)
    uint32_t window_ms = 10000;            // Sliding window for failures (10s)
    bool enable_global_hints = true;       // Cross-worker catastrophic failure hints
    uint32_t catastrophic_threshold = 20;  // Failures to trigger global hint
};

/// Upstream group configuration
struct UpstreamConfig {
    std::string name;
    std::vector<BackendConfig> backends;
    std::string load_balancing = "round_robin";  // round_robin, least_connections, random

    // Retry settings
    uint32_t max_retries = 2;
    uint32_t retry_timeout = 1000;  // milliseconds

    // Connection pool settings
    uint32_t pool_size = 100;
    uint32_t pool_idle_timeout = 60;  // seconds

    // Circuit breaker settings
    CircuitBreakerConfigSchema circuit_breaker;
};

/// Route configuration
struct RouteConfig {
    std::string path;
    std::string method = "GET";  // GET, POST, PUT, DELETE, etc. (empty = any)
    std::string upstream;        // Upstream name
    std::string handler_id;      // Optional handler identifier
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
    std::string key = "client_ip";  // client_ip, header:X-API-Key, etc.
};

/// Authentication configuration (simple token validation)
struct AuthConfig {
    bool enabled = false;
    std::string type = "bearer";  // bearer, basic, apikey
    std::string header = "Authorization";
    std::vector<std::string> valid_tokens;
};

/// JWT key configuration
struct JwtKeyConfig {
    std::string algorithm;          // "RS256", "ES256", "HS256"
    std::string key_id;             // Optional kid for rotation
    std::string public_key_path;    // PEM file for RS256/ES256
    std::string secret;             // For HS256 (base64-encoded)
};

/// JWKS (JSON Web Key Set) fetcher configuration
struct JwksConfigSchema {
    std::string url;                          // JWKS endpoint URL
    uint32_t refresh_interval_seconds = 3600; // Default: 1 hour
    uint32_t timeout_seconds = 10;            // HTTP timeout
    uint32_t retry_max = 3;                   // Max retries before circuit break
    uint32_t circuit_breaker_seconds = 300;   // Cooldown after failures (5 min)
};

/// JWT authentication configuration
struct JwtConfig {
    bool enabled = false;

    // Token extraction
    std::string header = "Authorization";  // Header name
    std::string scheme = "Bearer";         // "Bearer" or custom

    // Signature verification keys (static)
    std::vector<JwtKeyConfig> keys;

    // JWKS endpoint (dynamic key fetching)
    std::optional<JwksConfigSchema> jwks;

    // Claims validation
    bool require_exp = true;
    bool require_sub = false;
    std::vector<std::string> allowed_issuers;
    std::vector<std::string> allowed_audiences;
    int64_t clock_skew_seconds = 60;  // Tolerance for exp/nbf (clock drift)

    // Caching
    size_t cache_capacity = 10000;  // Tokens per thread
    bool cache_enabled = true;
};

/// Logging configuration
struct LogConfig {
    std::string level = "info";   // debug, info, warning, error
    std::string format = "json";  // json, text
    std::string output = "/var/log/titan";  // Log directory (worker_N.log appended)
    bool log_requests = true;
    bool log_responses = false;
    std::vector<std::string> exclude_paths;  // Don't log these paths

    struct RotationConfig {
        uint32_t max_size_mb = 100;
        uint32_t max_files = 10;
    } rotation;
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
    JwtConfig jwt;

    // Observability
    LogConfig logging;
    MetricsConfig metrics;

    // Metadata
    std::string version = "1.0";
    std::optional<std::string> description;
};

// nlohmann/json serialization macros (to_json only - from_json customized below)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ServerConfig, worker_threads, listen_address, listen_port,
                                   backlog, read_timeout, write_timeout, idle_timeout,
                                   shutdown_timeout, max_connections, max_request_size,
                                   max_header_size, tls_enabled, tls_certificate_path,
                                   tls_private_key_path, tls_alpn_protocols);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BackendConfig, host, port, weight, max_connections,
                                   health_check_enabled, health_check_interval,
                                   health_check_timeout, health_check_path);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CircuitBreakerConfigSchema, enabled, failure_threshold,
                                   success_threshold, timeout_ms, window_ms, enable_global_hints,
                                   catastrophic_threshold);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UpstreamConfig, name, backends, load_balancing, max_retries,
                                   retry_timeout, pool_size, pool_idle_timeout, circuit_breaker);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RouteConfig, path, method, upstream, handler_id, priority,
                                   rewrite_path, timeout, middleware);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CorsConfig, enabled, allowed_origins, allowed_methods,
                                   allowed_headers, allow_credentials, max_age);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RateLimitConfig, enabled, requests_per_second, burst_size, key);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AuthConfig, enabled, type, header, valid_tokens);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(JwtKeyConfig, algorithm, key_id, public_key_path, secret);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(JwksConfigSchema, url, refresh_interval_seconds, timeout_seconds,
                                   retry_max, circuit_breaker_seconds);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(JwtConfig, enabled, header, scheme, keys, jwks, require_exp,
                                   require_sub, allowed_issuers, allowed_audiences,
                                   clock_skew_seconds, cache_capacity, cache_enabled);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogConfig::RotationConfig, max_size_mb, max_files);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LogConfig, level, format, output, log_requests, log_responses,
                                   exclude_paths, rotation);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MetricsConfig, enabled, port, path, format);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Config, server, routes, upstreams, cors, rate_limit, auth, jwt,
                                   logging, metrics, version, description);

// Custom from_json functions to handle missing fields with defaults
inline void from_json(const nlohmann::json& j, ServerConfig& s) {
    s.worker_threads = j.value("worker_threads", 0u);
    s.listen_address = j.value("listen_address", std::string("0.0.0.0"));
    s.listen_port = j.value("listen_port", uint16_t(8080));
    s.backlog = j.value("backlog", 128u);
    s.read_timeout = j.value("read_timeout", 60000u);
    s.write_timeout = j.value("write_timeout", 60000u);
    s.idle_timeout = j.value("idle_timeout", 300000u);
    s.shutdown_timeout = j.value("shutdown_timeout", 30000u);
    s.max_connections = j.value("max_connections", 10000u);
    s.max_request_size = j.value("max_request_size", 1048576u);
    s.max_header_size = j.value("max_header_size", 8192u);
    s.tls_enabled = j.value("tls_enabled", false);
    s.tls_certificate_path = j.value("tls_certificate_path", std::string());
    s.tls_private_key_path = j.value("tls_private_key_path", std::string());
    s.tls_alpn_protocols =
        j.value("tls_alpn_protocols", std::vector<std::string>{"h2", "http/1.1"});
}

inline void from_json(const nlohmann::json& j, BackendConfig& b) {
    j.at("host").get_to(b.host);  // host is required
    b.port = j.value("port", uint16_t(80));
    b.weight = j.value("weight", 1u);
    b.max_connections = j.value("max_connections", 1000u);
    b.health_check_enabled = j.value("health_check_enabled", true);
    b.health_check_interval = j.value("health_check_interval", 30u);
    b.health_check_timeout = j.value("health_check_timeout", 5u);
    b.health_check_path = j.value("health_check_path", std::string("/health"));
}

inline void from_json(const nlohmann::json& j, CircuitBreakerConfigSchema& c) {
    c.enabled = j.value("enabled", true);
    c.failure_threshold = j.value("failure_threshold", 5u);
    c.success_threshold = j.value("success_threshold", 2u);
    c.timeout_ms = j.value("timeout_ms", 30000u);
    c.window_ms = j.value("window_ms", 10000u);
    c.enable_global_hints = j.value("enable_global_hints", true);
    c.catastrophic_threshold = j.value("catastrophic_threshold", 20u);
}

inline void from_json(const nlohmann::json& j, UpstreamConfig& u) {
    j.at("name").get_to(u.name);          // name is required
    j.at("backends").get_to(u.backends);  // backends is required
    u.load_balancing = j.value("load_balancing", std::string("round_robin"));
    u.max_retries = j.value("max_retries", 2u);
    u.retry_timeout = j.value("retry_timeout", 1000u);
    u.pool_size = j.value("pool_size", 100u);
    u.pool_idle_timeout = j.value("pool_idle_timeout", 60u);
    u.circuit_breaker = j.value("circuit_breaker", CircuitBreakerConfigSchema{});
}

inline void from_json(const nlohmann::json& j, RouteConfig& r) {
    j.at("path").get_to(r.path);          // path is required
    j.at("upstream").get_to(r.upstream);  // upstream is required
    r.method = j.value("method", std::string("GET"));
    r.handler_id = j.value("handler_id", std::string());
    r.priority = j.value("priority", 0u);
    r.rewrite_path = j.value("rewrite_path", std::optional<std::string>());
    r.timeout = j.value("timeout", std::optional<uint32_t>());
    r.middleware = j.value("middleware", std::vector<std::string>());
}

inline void from_json(const nlohmann::json& j, CorsConfig& c) {
    c.enabled = j.value("enabled", false);
    c.allowed_origins = j.value("allowed_origins", std::vector<std::string>{"*"});
    c.allowed_methods = j.value(
        "allowed_methods", std::vector<std::string>{"GET", "POST", "PUT", "DELETE", "OPTIONS"});
    c.allowed_headers = j.value("allowed_headers", std::vector<std::string>{"*"});
    c.allow_credentials = j.value("allow_credentials", false);
    c.max_age = j.value("max_age", 86400u);
}

inline void from_json(const nlohmann::json& j, RateLimitConfig& r) {
    r.enabled = j.value("enabled", false);
    r.requests_per_second = j.value("requests_per_second", 100u);
    r.burst_size = j.value("burst_size", 200u);
    r.key = j.value("key", std::string("client_ip"));
}

inline void from_json(const nlohmann::json& j, AuthConfig& a) {
    a.enabled = j.value("enabled", false);
    a.type = j.value("type", std::string("bearer"));
    a.header = j.value("header", std::string("Authorization"));
    a.valid_tokens = j.value("valid_tokens", std::vector<std::string>());
}

inline void from_json(const nlohmann::json& j, JwtKeyConfig& k) {
    j.at("algorithm").get_to(k.algorithm);  // algorithm is required
    k.key_id = j.value("key_id", std::string());
    k.public_key_path = j.value("public_key_path", std::string());
    k.secret = j.value("secret", std::string());
}

inline void from_json(const nlohmann::json& j, JwksConfigSchema& jwks) {
    j.at("url").get_to(jwks.url);  // url is required
    jwks.refresh_interval_seconds = j.value("refresh_interval_seconds", 3600u);
    jwks.timeout_seconds = j.value("timeout_seconds", 10u);
    jwks.retry_max = j.value("retry_max", 3u);
    jwks.circuit_breaker_seconds = j.value("circuit_breaker_seconds", 300u);
}

inline void from_json(const nlohmann::json& j, JwtConfig& jwt) {
    jwt.enabled = j.value("enabled", false);
    jwt.header = j.value("header", std::string("Authorization"));
    jwt.scheme = j.value("scheme", std::string("Bearer"));
    jwt.keys = j.value("keys", std::vector<JwtKeyConfig>());
    jwt.jwks = j.value("jwks", std::optional<JwksConfigSchema>());
    jwt.require_exp = j.value("require_exp", true);
    jwt.require_sub = j.value("require_sub", false);
    jwt.allowed_issuers = j.value("allowed_issuers", std::vector<std::string>());
    jwt.allowed_audiences = j.value("allowed_audiences", std::vector<std::string>());
    jwt.clock_skew_seconds = j.value("clock_skew_seconds", int64_t(60));
    jwt.cache_capacity = j.value("cache_capacity", size_t(10000));
    jwt.cache_enabled = j.value("cache_enabled", true);
}

inline void from_json(const nlohmann::json& j, LogConfig::RotationConfig& r) {
    r.max_size_mb = j.value("max_size_mb", 100u);
    r.max_files = j.value("max_files", 10u);
}

inline void from_json(const nlohmann::json& j, LogConfig& l) {
    l.level = j.value("level", std::string("info"));
    l.format = j.value("format", std::string("json"));
    l.output = j.value("output", std::string("/var/log/titan"));
    l.log_requests = j.value("log_requests", true);
    l.log_responses = j.value("log_responses", false);
    l.exclude_paths = j.value("exclude_paths", std::vector<std::string>());
    l.rotation = j.value("rotation", LogConfig::RotationConfig{});
}

inline void from_json(const nlohmann::json& j, MetricsConfig& m) {
    m.enabled = j.value("enabled", true);
    m.port = j.value("port", uint16_t(9090));
    m.path = j.value("path", std::string("/metrics"));
    m.format = j.value("format", std::string("prometheus"));
}

inline void from_json(const nlohmann::json& j, Config& c) {
    c.server = j.value("server", ServerConfig{});
    c.routes = j.value("routes", std::vector<RouteConfig>());
    c.upstreams = j.value("upstreams", std::vector<UpstreamConfig>());
    c.cors = j.value("cors", CorsConfig{});
    c.rate_limit = j.value("rate_limit", RateLimitConfig{});
    c.auth = j.value("auth", AuthConfig{});
    c.jwt = j.value("jwt", JwtConfig{});
    c.logging = j.value("logging", LogConfig{});
    c.metrics = j.value("metrics", MetricsConfig{});
    c.version = j.value("version", std::string("1.0"));
    c.description = j.value("description", std::optional<std::string>());
}

/// Configuration validation result
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void add_error(std::string error) {
        valid = false;
        errors.push_back(std::move(error));
    }

    void add_warning(std::string warning) { warnings.push_back(std::move(warning)); }

    [[nodiscard]] bool has_errors() const noexcept { return !valid || !errors.empty(); }
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
    [[nodiscard]] std::string_view config_path() const noexcept { return config_path_; }

    /// Check if configuration is loaded
    [[nodiscard]] bool is_loaded() const noexcept { return current_config_ != nullptr; }

    /// Get last validation result
    [[nodiscard]] const ValidationResult& last_validation() const noexcept {
        return last_validation_;
    }

private:
    std::string config_path_;
    std::shared_ptr<const Config> current_config_;
    ValidationResult last_validation_;
};

}  // namespace titan::control
