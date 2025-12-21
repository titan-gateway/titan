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

#include "../core/containers.hpp"
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
    bool route_cache_enabled = true;  // Thread-local LRU route cache
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

/// Path rewrite rule
struct PathRewriteRule {
    std::string type;         // "prefix_strip" or "regex"
    std::string pattern;      // Prefix to strip or regex pattern
    std::string replacement;  // Empty for prefix_strip, substitution string for regex
};

/// Header transformation rule
struct HeaderRule {
    std::string action;  // "add", "remove", "modify"
    std::string name;    // Header name (case-insensitive)
    std::string value;   // Header value (for add/modify)
};

/// Query parameter transformation rule
struct QueryRule {
    std::string action;  // "add", "remove", "modify"
    std::string name;    // Parameter name
    std::string value;   // Parameter value (for add/modify)
};

/// Request/Response transformation configuration
struct TransformConfig {
    bool enabled = false;

    // Path transformations (applied in order)
    std::vector<PathRewriteRule> path_rewrites;

    // Header transformations
    std::vector<HeaderRule> request_headers;   // Applied to request before backend
    std::vector<HeaderRule> response_headers;  // Applied to response from backend

    // Query parameter transformations (applied to request)
    std::vector<QueryRule> query_params;
};

/// Pre-compressed file serving configuration
struct PrecompressedConfig {
    bool enabled = true;
    std::vector<std::string> extensions = {".gz", ".zst", ".br"};
    std::string cache_control = "public, max-age=31536000";  // 1 year
};

/// Compression middleware configuration
struct CompressionConfig {
    bool enabled = false;
    size_t min_size = 1024;               // Don't compress responses < 1KB
    size_t streaming_threshold = 102400;  // Use streaming for responses > 100KB

    // Algorithm priority (negotiate with client Accept-Encoding)
    std::vector<std::string> algorithms = {"zstd", "gzip", "brotli"};

    // Content-Type filtering
    std::vector<std::string> content_types = {"text/html",        "text/plain",
                                              "text/css",         "text/xml",
                                              "application/json", "application/javascript",
                                              "application/xml",  "image/svg+xml"};

    // Excluded content types (takes precedence over content_types)
    std::vector<std::string> excluded_content_types = {"image/jpeg",
                                                       "image/png",
                                                       "image/gif",
                                                       "image/webp",
                                                       "video/mp4",
                                                       "video/mpeg",
                                                       "audio/mp3",
                                                       "audio/aac",
                                                       "application/zip",
                                                       "application/gzip",
                                                       "application/x-brotli",
                                                       "application/pdf",
                                                       "application/octet-stream"};

    // Compression levels per algorithm
    struct CompressionLevels {
        int gzip = 6;    // 1-9 (6 = balanced)
        int zstd = 5;    // 1-22 (5 = balanced)
        int brotli = 4;  // 0-11 (4 = balanced for dynamic content)
    } levels;

    // BREACH attack mitigation (disable compression for sensitive endpoints)
    // Paths matching these patterns will NOT be compressed (protects auth endpoints)
    // Example: ["/auth/*", "/login", "/api/csrf-token", "/api/token"]
    // User must explicitly configure - no hardcoded defaults
    std::vector<std::string> disable_for_paths;

    // Disable compression when response sets cookies (BREACH mitigation)
    // Cookies often contain session tokens which are vulnerable to BREACH
    // Safe default: enabled - works for all applications
    bool disable_when_setting_cookies = true;

    // Pre-compressed file serving
    PrecompressedConfig precompressed;
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

    // Request/Response transformation (per-route, overrides global)
    std::optional<TransformConfig> transform;

    // Compression (per-route, overrides global)
    std::optional<CompressionConfig> compression;

    // Authorization (JWT claims-based)
    bool auth_required = false;                // Require JWT authentication
    std::vector<std::string> required_scopes;  // OAuth 2.0 scopes (e.g., "read:users")
    std::vector<std::string> required_roles;   // Simple role strings (e.g., "admin")
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
    std::string algorithm;        // "RS256", "ES256", "HS256"
    std::string key_id;           // Optional kid for rotation
    std::string public_key_path;  // PEM file for RS256/ES256
    std::string secret;           // For HS256 (base64-encoded)
};

/// JWKS (JSON Web Key Set) fetcher configuration
struct JwksConfigSchema {
    std::string url;                           // JWKS endpoint URL
    uint32_t refresh_interval_seconds = 3600;  // Default: 1 hour
    uint32_t timeout_seconds = 10;             // HTTP timeout
    uint32_t retry_max = 3;                    // Max retries before circuit break
    uint32_t circuit_breaker_seconds = 300;    // Cooldown after failures (5 min)
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

    // Token revocation
    bool revocation_enabled = true;  // Enable token revocation checking
};

/// JWT authorization configuration
struct JwtAuthzConfig {
    bool enabled = true;                // Enable authorization middleware
    std::string scope_claim = "scope";  // JWT claim containing scopes
    std::string roles_claim = "roles";  // JWT claim containing roles
    bool require_all_scopes = false;    // true = AND, false = OR
    bool require_all_roles = false;     // true = AND, false = OR
};

/// Logging configuration
struct LogConfig {
    std::string level = "info";             // debug, info, warning, error
    std::string format = "json";            // json, text
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
    RateLimitConfig rate_limit;  // Global rate limit (backward compatibility)
    titan::core::fast_map<std::string, RateLimitConfig> rate_limits;  // Named rate limiters
    AuthConfig auth;
    JwtConfig jwt;
    JwtAuthzConfig jwt_authz;
    TransformConfig transform;      // Global transform config
    CompressionConfig compression;  // Global compression config

    // Observability
    LogConfig logging;
    MetricsConfig metrics;

    // Metadata
    std::string version = "1.0";
    std::optional<std::string> description;
};

// All config types use custom from_json/to_json (no macros - avoids conflicts)
// Custom functions defined below (after struct definitions)

// Custom from_json/to_json for JwtAuthzConfig to allow partial configs with defaults
inline void from_json(const nlohmann::json& j, JwtAuthzConfig& c) {
    c.enabled = j.value("enabled", false);
    c.scope_claim = j.value("scope_claim", std::string("scope"));
    c.roles_claim = j.value("roles_claim", std::string("roles"));
    c.require_all_scopes = j.value("require_all_scopes", false);
    c.require_all_roles = j.value("require_all_roles", false);
}

inline void to_json(nlohmann::json& j, const JwtAuthzConfig& c) {
    j = nlohmann::json{{"enabled", c.enabled},
                       {"scope_claim", c.scope_claim},
                       {"roles_claim", c.roles_claim},
                       {"require_all_scopes", c.require_all_scopes},
                       {"require_all_roles", c.require_all_roles}};
}

// Custom from_json/to_json for all types defined below

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

inline void from_json(const nlohmann::json& j, PathRewriteRule& p) {
    j.at("type").get_to(p.type);        // type is required
    j.at("pattern").get_to(p.pattern);  // pattern is required
    p.replacement = j.value("replacement", std::string());
}

inline void to_json(nlohmann::json& j, const PathRewriteRule& p) {
    j = nlohmann::json{{"type", p.type}, {"pattern", p.pattern}, {"replacement", p.replacement}};
}

inline void from_json(const nlohmann::json& j, HeaderRule& h) {
    j.at("action").get_to(h.action);  // action is required
    j.at("name").get_to(h.name);      // name is required
    h.value = j.value("value", std::string());
}

inline void to_json(nlohmann::json& j, const HeaderRule& h) {
    j = nlohmann::json{{"action", h.action}, {"name", h.name}, {"value", h.value}};
}

inline void from_json(const nlohmann::json& j, QueryRule& q) {
    j.at("action").get_to(q.action);  // action is required
    j.at("name").get_to(q.name);      // name is required
    q.value = j.value("value", std::string());
}

inline void to_json(nlohmann::json& j, const QueryRule& q) {
    j = nlohmann::json{{"action", q.action}, {"name", q.name}, {"value", q.value}};
}

inline void from_json(const nlohmann::json& j, TransformConfig& t) {
    t.enabled = j.value("enabled", false);

    // Use contains() for complex vector types to avoid infinite recursion
    if (j.contains("path_rewrites")) {
        j.at("path_rewrites").get_to(t.path_rewrites);
    }
    if (j.contains("request_headers")) {
        j.at("request_headers").get_to(t.request_headers);
    }
    if (j.contains("response_headers")) {
        j.at("response_headers").get_to(t.response_headers);
    }
    if (j.contains("query_params")) {
        j.at("query_params").get_to(t.query_params);
    }
}

inline void to_json(nlohmann::json& j, const TransformConfig& t) {
    j = nlohmann::json{{"enabled", t.enabled},
                       {"path_rewrites", t.path_rewrites},
                       {"request_headers", t.request_headers},
                       {"response_headers", t.response_headers},
                       {"query_params", t.query_params}};
}

// Compression serialization (must come before RouteConfig which uses it)
inline void from_json(const nlohmann::json& j, PrecompressedConfig& p) {
    p.enabled = j.value("enabled", true);
    p.extensions = j.value("extensions", std::vector<std::string>{".gz", ".zst", ".br"});
    p.cache_control = j.value("cache_control", std::string("public, max-age=31536000"));
}

inline void to_json(nlohmann::json& j, const PrecompressedConfig& p) {
    j = nlohmann::json{
        {"enabled", p.enabled}, {"extensions", p.extensions}, {"cache_control", p.cache_control}};
}

inline void from_json(const nlohmann::json& j, CompressionConfig::CompressionLevels& l) {
    l.gzip = j.value("gzip", 6);
    l.zstd = j.value("zstd", 5);
    l.brotli = j.value("brotli", 4);
}

inline void to_json(nlohmann::json& j, const CompressionConfig::CompressionLevels& l) {
    j = nlohmann::json{{"gzip", l.gzip}, {"zstd", l.zstd}, {"brotli", l.brotli}};
}

inline void from_json(const nlohmann::json& j, CompressionConfig& c) {
    c.enabled = j.value("enabled", false);
    c.min_size = j.value("min_size", size_t(1024));
    c.streaming_threshold = j.value("streaming_threshold", size_t(102400));
    c.algorithms = j.value("algorithms", std::vector<std::string>{"zstd", "gzip", "brotli"});
    c.content_types = j.value(
        "content_types", std::vector<std::string>{"text/html", "text/plain", "text/css", "text/xml",
                                                  "application/json", "application/javascript",
                                                  "application/xml", "image/svg+xml"});
    c.excluded_content_types =
        j.value("excluded_content_types",
                std::vector<std::string>{
                    "image/jpeg", "image/png", "image/gif", "image/webp", "video/mp4", "video/mpeg",
                    "audio/mp3", "audio/aac", "application/zip", "application/gzip",
                    "application/x-brotli", "application/pdf", "application/octet-stream"});
    c.levels = j.value("levels", CompressionConfig::CompressionLevels{});
    // BREACH mitigation - empty by default, user must configure
    c.disable_for_paths = j.value("disable_for_paths", std::vector<std::string>{});
    c.disable_when_setting_cookies = j.value("disable_when_setting_cookies", true);
    c.precompressed = j.value("precompressed", PrecompressedConfig{});
}

inline void to_json(nlohmann::json& j, const CompressionConfig& c) {
    j["enabled"] = c.enabled;
    j["min_size"] = c.min_size;
    j["streaming_threshold"] = c.streaming_threshold;
    j["algorithms"] = c.algorithms;
    j["content_types"] = c.content_types;
    j["excluded_content_types"] = c.excluded_content_types;
    j["levels"] = c.levels;
    j["disable_for_paths"] = c.disable_for_paths;
    j["disable_when_setting_cookies"] = c.disable_when_setting_cookies;
    j["precompressed"] = c.precompressed;
}

inline void from_json(const nlohmann::json& j, RouteConfig& r) {
    // Required fields
    j.at("path").get_to(r.path);
    j.at("upstream").get_to(r.upstream);

    // Optional fields - use contains() to avoid infinite recursion with complex types
    r.method = j.value("method", std::string("GET"));
    r.handler_id = j.value("handler_id", std::string());
    r.priority = j.value("priority", 0u);
    r.auth_required = j.value("auth_required", false);

    // Optional fields with complex types - must use contains() to avoid triggering to_json()
    if (j.contains("rewrite_path")) {
        j.at("rewrite_path").get_to(r.rewrite_path);
    }
    if (j.contains("timeout")) {
        j.at("timeout").get_to(r.timeout);
    }
    if (j.contains("middleware")) {
        j.at("middleware").get_to(r.middleware);
    }
    if (j.contains("transform")) {
        j.at("transform").get_to(r.transform);
    }
    if (j.contains("compression")) {
        j.at("compression").get_to(r.compression);
    }
    if (j.contains("required_scopes")) {
        j.at("required_scopes").get_to(r.required_scopes);
    }
    if (j.contains("required_roles")) {
        j.at("required_roles").get_to(r.required_roles);
    }
}

inline void to_json(nlohmann::json& j, const RouteConfig& r) {
    j["path"] = r.path;
    j["method"] = r.method;
    j["upstream"] = r.upstream;
    j["handler_id"] = r.handler_id;
    j["priority"] = r.priority;
    j["rewrite_path"] = r.rewrite_path;
    j["timeout"] = r.timeout;
    j["middleware"] = r.middleware;
    j["transform"] = r.transform;
    j["compression"] = r.compression;
    j["auth_required"] = r.auth_required;
    j["required_scopes"] = r.required_scopes;
    j["required_roles"] = r.required_roles;
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

    // Use contains() for custom struct types to avoid infinite recursion
    if (j.contains("keys")) {
        j.at("keys").get_to(jwt.keys);
    }
    if (j.contains("jwks")) {
        j.at("jwks").get_to(jwt.jwks);
    }

    jwt.require_exp = j.value("require_exp", true);
    jwt.require_sub = j.value("require_sub", false);
    jwt.allowed_issuers = j.value("allowed_issuers", std::vector<std::string>());
    jwt.allowed_audiences = j.value("allowed_audiences", std::vector<std::string>());
    jwt.clock_skew_seconds = j.value("clock_skew_seconds", int64_t(60));
    jwt.cache_capacity = j.value("cache_capacity", size_t(10000));
    jwt.cache_enabled = j.value("cache_enabled", true);
    jwt.revocation_enabled = j.value("revocation_enabled", true);
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
    // Use contains() + get() instead of value() to avoid infinite recursion
    // when default values trigger to_json() -> from_json() cycles
    if (j.contains("server")) {
        j.at("server").get_to(c.server);
    }
    if (j.contains("routes")) {
        j.at("routes").get_to(c.routes);
    }
    if (j.contains("upstreams")) {
        j.at("upstreams").get_to(c.upstreams);
    }
    if (j.contains("cors")) {
        j.at("cors").get_to(c.cors);
    }
    if (j.contains("rate_limit")) {
        j.at("rate_limit").get_to(c.rate_limit);
    }
    if (j.contains("rate_limits")) {
        j.at("rate_limits").get_to(c.rate_limits);
    }
    if (j.contains("auth")) {
        j.at("auth").get_to(c.auth);
    }
    if (j.contains("jwt")) {
        j.at("jwt").get_to(c.jwt);
    }
    if (j.contains("jwt_authz")) {
        j.at("jwt_authz").get_to(c.jwt_authz);
    }
    if (j.contains("transform")) {
        j.at("transform").get_to(c.transform);
    }
    if (j.contains("compression")) {
        j.at("compression").get_to(c.compression);
    }
    if (j.contains("logging")) {
        j.at("logging").get_to(c.logging);
    }
    if (j.contains("metrics")) {
        j.at("metrics").get_to(c.metrics);
    }
    if (j.contains("version")) {
        j.at("version").get_to(c.version);
    }
    if (j.contains("description")) {
        j.at("description").get_to(c.description);
    }
}

// ============================================================================
// to_json functions for all config types
// ============================================================================

inline void to_json(nlohmann::json& j, const ServerConfig& s) {
    j = nlohmann::json{{"worker_threads", s.worker_threads},
                       {"listen_address", s.listen_address},
                       {"listen_port", s.listen_port},
                       {"backlog", s.backlog},
                       {"read_timeout", s.read_timeout},
                       {"write_timeout", s.write_timeout},
                       {"idle_timeout", s.idle_timeout},
                       {"shutdown_timeout", s.shutdown_timeout},
                       {"max_connections", s.max_connections},
                       {"max_request_size", s.max_request_size},
                       {"max_header_size", s.max_header_size},
                       {"tls_enabled", s.tls_enabled},
                       {"tls_certificate_path", s.tls_certificate_path},
                       {"tls_private_key_path", s.tls_private_key_path},
                       {"tls_alpn_protocols", s.tls_alpn_protocols}};
}

inline void to_json(nlohmann::json& j, const BackendConfig& b) {
    j = nlohmann::json{{"host", b.host},
                       {"port", b.port},
                       {"weight", b.weight},
                       {"max_connections", b.max_connections},
                       {"health_check_enabled", b.health_check_enabled},
                       {"health_check_interval", b.health_check_interval},
                       {"health_check_timeout", b.health_check_timeout},
                       {"health_check_path", b.health_check_path}};
}

inline void to_json(nlohmann::json& j, const CircuitBreakerConfigSchema& c) {
    j = nlohmann::json{{"enabled", c.enabled},
                       {"failure_threshold", c.failure_threshold},
                       {"success_threshold", c.success_threshold},
                       {"timeout_ms", c.timeout_ms},
                       {"window_ms", c.window_ms},
                       {"enable_global_hints", c.enable_global_hints},
                       {"catastrophic_threshold", c.catastrophic_threshold}};
}

inline void to_json(nlohmann::json& j, const UpstreamConfig& u) {
    j = nlohmann::json{{"name", u.name},
                       {"backends", u.backends},
                       {"load_balancing", u.load_balancing},
                       {"max_retries", u.max_retries},
                       {"retry_timeout", u.retry_timeout},
                       {"pool_size", u.pool_size},
                       {"pool_idle_timeout", u.pool_idle_timeout},
                       {"circuit_breaker", u.circuit_breaker}};
}

inline void to_json(nlohmann::json& j, const CorsConfig& c) {
    j = nlohmann::json{{"enabled", c.enabled},
                       {"allowed_origins", c.allowed_origins},
                       {"allowed_methods", c.allowed_methods},
                       {"allowed_headers", c.allowed_headers},
                       {"allow_credentials", c.allow_credentials},
                       {"max_age", c.max_age}};
}

inline void to_json(nlohmann::json& j, const RateLimitConfig& r) {
    j = nlohmann::json{{"enabled", r.enabled},
                       {"requests_per_second", r.requests_per_second},
                       {"burst_size", r.burst_size},
                       {"key", r.key}};
}

inline void to_json(nlohmann::json& j, const AuthConfig& a) {
    j = nlohmann::json{{"enabled", a.enabled},
                       {"type", a.type},
                       {"header", a.header},
                       {"valid_tokens", a.valid_tokens}};
}

inline void to_json(nlohmann::json& j, const JwtKeyConfig& k) {
    j = nlohmann::json{{"algorithm", k.algorithm},
                       {"key_id", k.key_id},
                       {"public_key_path", k.public_key_path},
                       {"secret", k.secret}};
}

inline void to_json(nlohmann::json& j, const JwksConfigSchema& k) {
    j = nlohmann::json{{"url", k.url},
                       {"refresh_interval_seconds", k.refresh_interval_seconds},
                       {"timeout_seconds", k.timeout_seconds},
                       {"retry_max", k.retry_max},
                       {"circuit_breaker_seconds", k.circuit_breaker_seconds}};
}

inline void to_json(nlohmann::json& j, const JwtConfig& jwt) {
    j = nlohmann::json{{"enabled", jwt.enabled},
                       {"header", jwt.header},
                       {"scheme", jwt.scheme},
                       {"keys", jwt.keys},
                       {"jwks", jwt.jwks},
                       {"require_exp", jwt.require_exp},
                       {"require_sub", jwt.require_sub},
                       {"allowed_issuers", jwt.allowed_issuers},
                       {"allowed_audiences", jwt.allowed_audiences},
                       {"clock_skew_seconds", jwt.clock_skew_seconds},
                       {"cache_capacity", jwt.cache_capacity},
                       {"cache_enabled", jwt.cache_enabled}};
}

inline void to_json(nlohmann::json& j, const LogConfig::RotationConfig& r) {
    j = nlohmann::json{{"max_size_mb", r.max_size_mb}, {"max_files", r.max_files}};
}

inline void to_json(nlohmann::json& j, const LogConfig& l) {
    j = nlohmann::json{{"level", l.level},
                       {"format", l.format},
                       {"output", l.output},
                       {"log_requests", l.log_requests},
                       {"log_responses", l.log_responses},
                       {"exclude_paths", l.exclude_paths},
                       {"rotation", l.rotation}};
}

inline void to_json(nlohmann::json& j, const MetricsConfig& m) {
    j = nlohmann::json{
        {"enabled", m.enabled}, {"port", m.port}, {"path", m.path}, {"format", m.format}};
}

inline void to_json(nlohmann::json& j, const Config& c) {
    j["server"] = c.server;
    j["routes"] = c.routes;
    j["upstreams"] = c.upstreams;
    j["cors"] = c.cors;
    j["rate_limit"] = c.rate_limit;
    j["rate_limits"] = c.rate_limits;
    j["auth"] = c.auth;
    j["jwt"] = c.jwt;
    j["jwt_authz"] = c.jwt_authz;
    j["transform"] = c.transform;
    j["compression"] = c.compression;
    j["logging"] = c.logging;
    j["metrics"] = c.metrics;
    j["version"] = c.version;
    j["description"] = c.description;
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
