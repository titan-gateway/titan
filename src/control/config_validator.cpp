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

// Config Validator - Implementation

#include "config_validator.hpp"

#include <algorithm>
#include <sstream>

#include "../core/containers.hpp"
#include "../core/string_utils.hpp"

namespace titan::control {

ValidationResult ConfigValidator::validate(const Config& config) {
    ValidationResult result;

    // Validate middleware references (typo detection)
    validate_middleware_references(config, result);

    // Validate security policies (no auth on sensitive paths - user rejected)
    validate_security_policies(config, result);

    // Validate for duplicate middleware of same type
    validate_middleware_duplicates(config, result);

    return result;
}

void ConfigValidator::validate_middleware_references(const Config& config,
                                                      ValidationResult& result) {
    // Collect all available middleware names
    std::vector<std::string> available_middleware = get_all_middleware_names(config);

    // Check each route's middleware references
    for (size_t i = 0; i < config.routes.size(); ++i) {
        const auto& route = config.routes[i];

        for (const auto& middleware_name : route.middleware) {
            // Check if middleware exists
            if (!middleware_exists(config, middleware_name)) {
                // Unknown middleware - find similar names
                std::string suggestion = suggest_similar_middleware(config, middleware_name);

                std::ostringstream msg;
                msg << "Route #" << i << " (" << route.path << "): Unknown middleware '"
                    << middleware_name << "'";

                if (!suggestion.empty()) {
                    msg << ". Did you mean: " << suggestion;
                }

                result.add_error(msg.str());
            }
        }
    }
}

void ConfigValidator::validate_security_policies(const Config& /* config */,
                                                  ValidationResult& /* result */) {
    // User rejected "Auth on Sensitive Paths" validation as "just stupid"
    // No security policy validation for now
}

void ConfigValidator::validate_middleware_duplicates(const Config& config,
                                                      ValidationResult& result) {
    // Track middleware types seen in each route
    for (size_t i = 0; i < config.routes.size(); ++i) {
        const auto& route = config.routes[i];
        titan::core::fast_set<std::string> types_seen;

        for (const auto& middleware_name : route.middleware) {
            // Determine middleware type from name
            std::string type;

            // Check which config map contains this middleware
            if (config.rate_limits.contains(middleware_name)) {
                type = "rate_limit";
            } else if (config.cors_configs.contains(middleware_name)) {
                type = "cors";
            } else if (config.transform_configs.contains(middleware_name)) {
                type = "transform";
            } else if (config.compression_configs.contains(middleware_name)) {
                type = "compression";
            }

            if (!type.empty()) {
                if (types_seen.contains(type)) {
                    std::ostringstream msg;
                    msg << "Route #" << i << " (" << route.path
                        << "): Multiple middleware of same type '" << type
                        << "'. Only the first will execute (REPLACEMENT model).";
                    result.add_warning(msg.str());
                }
                types_seen.insert(type);
            }
        }
    }
}

bool ConfigValidator::middleware_exists(const Config& config, const std::string& name) {
    // Check all middleware maps
    return config.rate_limits.contains(name) || config.cors_configs.contains(name) ||
           config.transform_configs.contains(name) || config.compression_configs.contains(name);
}

std::vector<std::string> ConfigValidator::get_all_middleware_names(const Config& config) {
    std::vector<std::string> names;

    // Collect from all middleware maps
    for (const auto& [name, _] : config.rate_limits) {
        names.push_back(name);
    }
    for (const auto& [name, _] : config.cors_configs) {
        names.push_back(name);
    }
    for (const auto& [name, _] : config.transform_configs) {
        names.push_back(name);
    }
    for (const auto& [name, _] : config.compression_configs) {
        names.push_back(name);
    }

    return names;
}

std::string ConfigValidator::suggest_similar_middleware(const Config& config,
                                                        const std::string& typo) {
    std::vector<std::string> available = get_all_middleware_names(config);

    // Find similar strings (max edit distance 3)
    std::vector<std::string> similar = titan::core::find_similar_strings(typo, available, 3);

    // Return top suggestion (or empty if none)
    if (!similar.empty()) {
        // If multiple suggestions, join with ", "
        if (similar.size() == 1) {
            return similar[0];
        } else {
            return titan::core::join(similar, ", ");
        }
    }

    return "";
}

}  // namespace titan::control
