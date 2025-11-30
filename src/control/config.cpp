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

namespace titan::control {

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
        // Parse error
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

    // Validate logging level
    if (config.logging.level != "debug" && config.logging.level != "info" &&
        config.logging.level != "warning" && config.logging.level != "error") {
        result.add_error("Unknown logging level '" + config.logging.level + "'");
    }

    // Validate logging format
    if (config.logging.format != "json" && config.logging.format != "text") {
        result.add_error("Unknown logging format '" + config.logging.format + "'");
    }

    return result;
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
