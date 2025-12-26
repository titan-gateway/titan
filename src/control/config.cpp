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

// Titan Configuration - Implementation

#include "config.hpp"

#include <atomic>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "../core/jwt.hpp"    // For security constants (MAX_REQUIRED_SCOPES_ROLES)
#include "../http/regex.hpp"  // For regex pattern validation

namespace titan::control {

// Forward declaration for helper function
static void validate_transform_config(const TransformConfig& transform, const std::string& context,
                                      ValidationResult& result);

// ConfigLoader implementation

std::optional<Config> ConfigLoader::load_from_file(std::string_view path) {
    // Read file contents
    std::string path_str{path};
    std::ifstream file{path_str};
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    return load_from_json(json);
}

std::optional<Config> ConfigLoader::load_from_json(std::string_view json) {
    Config config;

    try {
        auto j = nlohmann::json::parse(json);
        config = j.get<Config>();
    } catch (const nlohmann::json::exception& e) {
        // Parse error - log detailed error message
        fprintf(stderr, "JSON parsing error: %s\n", e.what());
        return std::nullopt;
    }

    // Validate configuration
    auto validation = validate(config);

    if (validation.has_errors()) {
        return std::nullopt;
    }

    return config;
}

ValidationResult ConfigLoader::validate(const Config& config) {
    ValidationResult result;

    // Validate server configuration
    if (config.server.listen_port == 0) {
        result.add_error("Server listen_port must be > 0");
    }

    if (config.server.max_request_size == 0) {
        result.add_error("Server max_request_size must be > 0");
    }

    if (config.server.max_header_size == 0) {
        result.add_error("Server max_header_size must be > 0");
    }

    // Validate upstreams
    if (config.upstreams.empty()) {
        result.add_warning("No upstreams configured");
    }

    for (const auto& upstream : config.upstreams) {
        if (upstream.name.empty()) {
            result.add_error("Upstream name cannot be empty");
        }

        if (upstream.backends.empty()) {
            result.add_error("Upstream '" + upstream.name + "' has no backends");
        }

        for (const auto& backend : upstream.backends) {
            if (backend.host.empty()) {
                result.add_error("Backend host cannot be empty in upstream '" + upstream.name +
                                 "'");
            }

            if (backend.port == 0) {
                result.add_error("Backend port must be > 0 in upstream '" + upstream.name + "'");
            }

            if (backend.weight == 0) {
                result.add_warning("Backend weight is 0 in upstream '" + upstream.name +
                                   "' (will not receive traffic)");
            }
        }

        // Validate load balancing strategy
        if (upstream.load_balancing != "round_robin" &&
            upstream.load_balancing != "least_connections" && upstream.load_balancing != "random" &&
            upstream.load_balancing != "weighted_round_robin") {
            result.add_error("Unknown load_balancing strategy '" + upstream.load_balancing +
                             "' in upstream '" + upstream.name + "'");
        }
    }

    // Validate routes
    if (config.routes.empty()) {
        result.add_warning("No routes configured");
    }

    for (const auto& route : config.routes) {
        if (route.path.empty()) {
            result.add_error("Route path cannot be empty");
        }

        if (route.upstream.empty()) {
            result.add_error("Route '" + route.path + "' has no upstream");
        }

        // Check that upstream exists
        bool upstream_found = false;
        for (const auto& upstream : config.upstreams) {
            if (upstream.name == route.upstream) {
                upstream_found = true;
                break;
            }
        }

        // Security: Validate authorization requirements (DoS prevention)
        if (route.required_scopes.size() > core::MAX_REQUIRED_SCOPES_ROLES) {
            result.add_error("Route '" + route.path + "' has too many required_scopes (" +
                             std::to_string(route.required_scopes.size()) + " > " +
                             std::to_string(core::MAX_REQUIRED_SCOPES_ROLES) + ")");
        }

        if (route.required_roles.size() > core::MAX_REQUIRED_SCOPES_ROLES) {
            result.add_error("Route '" + route.path + "' has too many required_roles (" +
                             std::to_string(route.required_roles.size()) + " > " +
                             std::to_string(core::MAX_REQUIRED_SCOPES_ROLES) + ")");
        }

        if (!upstream_found) {
            result.add_error("Route '" + route.path + "' references non-existent upstream '" +
                             route.upstream + "'");
        }

        // Validate HTTP method
        if (!route.method.empty()) {
            if (route.method != "GET" && route.method != "POST" && route.method != "PUT" &&
                route.method != "DELETE" && route.method != "HEAD" && route.method != "OPTIONS" &&
                route.method != "PATCH" && route.method != "CONNECT" && route.method != "TRACE") {
                result.add_error("Unknown HTTP method '" + route.method + "' in route '" +
                                 route.path + "'");
            }
        }
    }

    // Validate global transform configuration
    if (config.transform.enabled) {
        validate_transform_config(config.transform, "global", result);
    }

    // Validate per-route transform configurations
    for (const auto& route : config.routes) {
        if (route.transform.has_value() && route.transform->enabled) {
            validate_transform_config(*route.transform, "route '" + route.path + "'", result);
        }
    }

    // Validate logging level
    if (config.logging.level != "debug" && config.logging.level != "info" &&
        config.logging.level != "warning" && config.logging.level != "error") {
        result.add_error("Unknown logging level '" + config.logging.level + "'");
    }

    // Validate logging format
    if (config.logging.format != "json" && config.logging.format != "text") {
        result.add_error("Unknown logging format '" + config.logging.format + "'");
    }

    // Validate WebSocket server configuration
    if (config.server.websocket.has_value()) {
        const auto& ws = *config.server.websocket;

        // Frame size limits (DoS prevention)
        if (ws.max_frame_size == 0 || ws.max_frame_size > MAX_WEBSOCKET_FRAME_SIZE) {
            result.add_error("WebSocket max_frame_size must be 1-" +
                             std::to_string(MAX_WEBSOCKET_FRAME_SIZE) + " bytes");
        }

        // Message size must be >= frame size
        if (ws.max_message_size < ws.max_frame_size) {
            result.add_error("WebSocket max_message_size must be >= max_frame_size");
        }

        if (ws.max_message_size > MAX_WEBSOCKET_MESSAGE_SIZE) {
            result.add_error("WebSocket max_message_size exceeds limit (" +
                             std::to_string(MAX_WEBSOCKET_MESSAGE_SIZE) + " bytes)");
        }

        // Timeout validation
        if (ws.idle_timeout == 0 || ws.idle_timeout > MAX_WEBSOCKET_IDLE_TIMEOUT) {
            result.add_error("WebSocket idle_timeout must be 1-" +
                             std::to_string(MAX_WEBSOCKET_IDLE_TIMEOUT) + " seconds");
        }

        // Ping interval must be < idle timeout
        if (ws.ping_interval >= ws.idle_timeout) {
            result.add_error("WebSocket ping_interval must be < idle_timeout");
        }

        // Ping interval minimum
        if (ws.ping_interval < MIN_WEBSOCKET_PING_INTERVAL) {
            result.add_error("WebSocket ping_interval must be >= " +
                             std::to_string(MIN_WEBSOCKET_PING_INTERVAL) + " second");
        }

        // Pong timeout validation
        if (ws.pong_timeout >= ws.ping_interval) {
            result.add_warning("WebSocket pong_timeout >= ping_interval may cause false positives");
        }

        // Connection limits
        if (ws.max_connections_per_worker == 0) {
            result.add_error("WebSocket max_connections_per_worker must be > 0");
        }

        if (ws.max_connections_per_worker > MAX_WEBSOCKET_CONNECTIONS_PER_WORKER) {
            result.add_warning("WebSocket max_connections_per_worker exceeds recommended limit (" +
                               std::to_string(MAX_WEBSOCKET_CONNECTIONS_PER_WORKER) + ")");
        }

        // Frame rate limit
        if (ws.max_frames_per_second == 0) {
            result.add_error("WebSocket max_frames_per_second must be > 0");
        }
    }

    // Validate WebSocket route configuration
    for (const auto& route : config.routes) {
        if (route.websocket.has_value()) {
            const auto& ws = *route.websocket;

            // WebSocket only works with GET method
            if (!route.method.empty() && route.method != "GET") {
                result.add_error("Route '" + route.path +
                                 "': WebSocket requires method=GET, got '" + route.method + "'");
            }

            // Subprotocols validation (security)
            if (ws.subprotocols.size() > MAX_WEBSOCKET_SUBPROTOCOLS) {
                result.add_error("Route '" + route.path + "': Too many subprotocols (max " +
                                 std::to_string(MAX_WEBSOCKET_SUBPROTOCOLS) + ")");
            }

            for (const auto& subproto : ws.subprotocols) {
                if (subproto.empty() || subproto.length() > MAX_SUBPROTOCOL_NAME_LENGTH) {
                    result.add_error("Route '" + route.path +
                                     "': Invalid subprotocol name (empty or too long)");
                    break;
                }

                // Character whitelist (RFC 6455: token characters)
                for (char c : subproto) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' &&
                        c != '.') {
                        result.add_error("Route '" + route.path + "': Subprotocol '" + subproto +
                                         "' contains invalid character '" + c +
                                         "' (only alphanumeric, '_', '-', '.' allowed)");
                        break;
                    }
                }
            }

            // Timeout overrides must be reasonable
            if (ws.idle_timeout.has_value()) {
                if (*ws.idle_timeout == 0 || *ws.idle_timeout > MAX_WEBSOCKET_IDLE_TIMEOUT) {
                    result.add_error("Route '" + route.path + "': Invalid idle_timeout override");
                }
            }

            if (ws.ping_interval.has_value()) {
                if (*ws.ping_interval < MIN_WEBSOCKET_PING_INTERVAL) {
                    result.add_error("Route '" + route.path +
                                     "': Invalid ping_interval override (too small)");
                }

                // Check ping < idle if both are set
                if (ws.idle_timeout.has_value() && *ws.ping_interval >= *ws.idle_timeout) {
                    result.add_error("Route '" + route.path +
                                     "': ping_interval must be < idle_timeout");
                }
            }

            if (ws.max_connections.has_value() && *ws.max_connections == 0) {
                result.add_error("Route '" + route.path + "': max_connections must be > 0");
            }
        }
    }

    // Validate WebSocket upstream configuration
    for (const auto& upstream : config.upstreams) {
        if (upstream.websocket.has_value()) {
            const auto& ws = *upstream.websocket;

            // Backend ping interval validation
            if (ws.backend_ping_interval < MIN_WEBSOCKET_PING_INTERVAL) {
                result.add_error("Upstream '" + upstream.name +
                                 "': backend_ping_interval too small (min " +
                                 std::to_string(MIN_WEBSOCKET_PING_INTERVAL) + " second)");
            }
        }
    }

    return result;
}

// Helper function to validate TransformConfig
static void validate_transform_config(const TransformConfig& transform, const std::string& context,
                                      ValidationResult& result) {
    // Security limits to prevent DoS
    constexpr size_t MAX_PATH_REWRITES = 20;
    constexpr size_t MAX_HEADER_RULES = 50;
    constexpr size_t MAX_QUERY_RULES = 30;
    constexpr size_t MAX_HEADER_VALUE_SIZE = 8192;  // 8KB
    constexpr size_t MAX_PATTERN_SIZE = 1024;

    // Validate path rewrites
    if (transform.path_rewrites.size() > MAX_PATH_REWRITES) {
        result.add_error(context + ": too many path_rewrites (" +
                         std::to_string(transform.path_rewrites.size()) + " > " +
                         std::to_string(MAX_PATH_REWRITES) + ")");
    }

    for (size_t i = 0; i < transform.path_rewrites.size(); ++i) {
        const auto& rule = transform.path_rewrites[i];
        std::string rule_context = context + " path_rewrite[" + std::to_string(i) + "]";

        if (rule.type != "prefix_strip" && rule.type != "regex") {
            result.add_error(rule_context + ": invalid type '" + rule.type +
                             "' (must be 'prefix_strip' or 'regex')");
        }

        if (rule.pattern.empty()) {
            result.add_error(rule_context + ": pattern cannot be empty");
        }

        if (rule.pattern.size() > MAX_PATTERN_SIZE) {
            result.add_error(rule_context + ": pattern too large (" +
                             std::to_string(rule.pattern.size()) + " > " +
                             std::to_string(MAX_PATTERN_SIZE) + ")");
        }

        // For regex type, validate pattern compiles
        if (rule.type == "regex") {
            // Import Regex class
            auto regex_result = http::Regex::compile(rule.pattern);
            if (!regex_result.has_value()) {
                result.add_error(rule_context + ": invalid regex pattern '" + rule.pattern + "'");
            }
        }
    }

    // Validate request headers
    if (transform.request_headers.size() > MAX_HEADER_RULES) {
        result.add_error(context + ": too many request_headers (" +
                         std::to_string(transform.request_headers.size()) + " > " +
                         std::to_string(MAX_HEADER_RULES) + ")");
    }

    for (size_t i = 0; i < transform.request_headers.size(); ++i) {
        const auto& rule = transform.request_headers[i];
        std::string rule_context = context + " request_header[" + std::to_string(i) + "]";

        if (rule.action != "add" && rule.action != "remove" && rule.action != "modify") {
            result.add_error(rule_context + ": invalid action '" + rule.action +
                             "' (must be 'add', 'remove', or 'modify')");
        }

        if (rule.name.empty()) {
            result.add_error(rule_context + ": name cannot be empty");
        }

        if ((rule.action == "add" || rule.action == "modify") && rule.value.empty()) {
            result.add_warning(rule_context + ": value is empty for action '" + rule.action + "'");
        }

        if (rule.value.size() > MAX_HEADER_VALUE_SIZE) {
            result.add_error(rule_context + ": value too large (" +
                             std::to_string(rule.value.size()) + " > " +
                             std::to_string(MAX_HEADER_VALUE_SIZE) + ")");
        }
    }

    // Validate response headers
    if (transform.response_headers.size() > MAX_HEADER_RULES) {
        result.add_error(context + ": too many response_headers (" +
                         std::to_string(transform.response_headers.size()) + " > " +
                         std::to_string(MAX_HEADER_RULES) + ")");
    }

    for (size_t i = 0; i < transform.response_headers.size(); ++i) {
        const auto& rule = transform.response_headers[i];
        std::string rule_context = context + " response_header[" + std::to_string(i) + "]";

        if (rule.action != "add" && rule.action != "remove" && rule.action != "modify") {
            result.add_error(rule_context + ": invalid action '" + rule.action +
                             "' (must be 'add', 'remove', or 'modify')");
        }

        if (rule.name.empty()) {
            result.add_error(rule_context + ": name cannot be empty");
        }

        if ((rule.action == "add" || rule.action == "modify") && rule.value.empty()) {
            result.add_warning(rule_context + ": value is empty for action '" + rule.action + "'");
        }

        if (rule.value.size() > MAX_HEADER_VALUE_SIZE) {
            result.add_error(rule_context + ": value too large (" +
                             std::to_string(rule.value.size()) + " > " +
                             std::to_string(MAX_HEADER_VALUE_SIZE) + ")");
        }
    }

    // Validate query params
    if (transform.query_params.size() > MAX_QUERY_RULES) {
        result.add_error(context + ": too many query_params (" +
                         std::to_string(transform.query_params.size()) + " > " +
                         std::to_string(MAX_QUERY_RULES) + ")");
    }

    for (size_t i = 0; i < transform.query_params.size(); ++i) {
        const auto& rule = transform.query_params[i];
        std::string rule_context = context + " query_param[" + std::to_string(i) + "]";

        if (rule.action != "add" && rule.action != "remove" && rule.action != "modify") {
            result.add_error(rule_context + ": invalid action '" + rule.action +
                             "' (must be 'add', 'remove', or 'modify')");
        }

        if (rule.name.empty()) {
            result.add_error(rule_context + ": name cannot be empty");
        }

        if ((rule.action == "add" || rule.action == "modify") && rule.value.empty()) {
            result.add_warning(rule_context + ": value is empty for action '" + rule.action + "'");
        }
    }
}

bool ConfigLoader::save_to_file(const Config& config, std::string_view path) {
    std::string json = to_json(config);
    if (json.empty()) {
        return false;
    }

    std::string path_str{path};
    std::ofstream file{path_str};
    if (!file.is_open()) {
        return false;
    }

    file << json;
    return file.good();
}

std::string ConfigLoader::to_json(const Config& config) {
    try {
        nlohmann::json j = config;
        return j.dump(2);  // 2-space indentation
    } catch (const nlohmann::json::exception& e) {
        return "";
    }
}

// ConfigManager implementation

bool ConfigManager::load(std::string_view path) {
    config_path_ = path;

    auto maybe_config = ConfigLoader::load_from_file(path);
    if (!maybe_config.has_value()) {
        return false;
    }

    // Validate configuration
    last_validation_ = ConfigLoader::validate(*maybe_config);
    if (last_validation_.has_errors()) {
        return false;
    }

    // Store configuration (atomic swap)
    current_config_ = std::make_shared<const Config>(std::move(*maybe_config));

    return true;
}

bool ConfigManager::reload() {
    if (config_path_.empty()) {
        return false;
    }

    auto maybe_config = ConfigLoader::load_from_file(config_path_);
    if (!maybe_config.has_value()) {
        return false;
    }

    // Validate configuration
    last_validation_ = ConfigLoader::validate(*maybe_config);
    if (last_validation_.has_errors()) {
        return false;
    }

    // RCU pattern: Create new shared_ptr and atomically swap
    // Old config remains valid until all readers release their references
    auto new_config = std::make_shared<const Config>(std::move(*maybe_config));

    // Atomic swap - this is the critical section for hot-reload
    std::atomic_store(&current_config_, new_config);

    return true;
}

std::shared_ptr<const Config> ConfigManager::get() const noexcept {
    // Atomic load - safe for concurrent readers
    return std::atomic_load(&current_config_);
}

}  // namespace titan::control
