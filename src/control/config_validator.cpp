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
#include <cctype>
#include <sstream>

#include "../core/containers.hpp"
#include "../core/string_utils.hpp"

namespace titan::control {

namespace {

/// Validate middleware name for security (prevent injection, path traversal, DoS)
/// Returns empty string if valid, error message if invalid
[[nodiscard]] std::string validate_middleware_name_security(std::string_view name) {
    // Length check (prevent DoS via long names)
    if (name.empty()) {
        return "Middleware name cannot be empty";
    }
    if (name.length() > MAX_MIDDLEWARE_NAME_LENGTH) {
        std::ostringstream msg;
        msg << "Middleware name too long (" << name.length() << " > " << MAX_MIDDLEWARE_NAME_LENGTH
            << " chars)";
        return msg.str();
    }

    // Character whitelist: [a-zA-Z0-9_-] only (prevent injection attacks)
    for (size_t i = 0; i < name.length(); ++i) {
        char c = name[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            std::ostringstream msg;
            msg << "Invalid character '" << c << "' at position " << i
                << " (only alphanumeric, underscore, and hyphen allowed)";
            return msg.str();
        }
    }

    // Path traversal prevention
    if (name.find("..") != std::string_view::npos) {
        return "Path traversal detected (..)";
    }
    if (name.find("./") != std::string_view::npos || name.find(".\\") != std::string_view::npos) {
        return "Path traversal detected (./)";
    }
    if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos) {
        return "Path separators not allowed";
    }

    // Null byte check (prevent null byte injection)
    if (name.find('\0') != std::string_view::npos) {
        return "Null byte detected";
    }

    // CRLF check (prevent CRLF injection)
    if (name.find('\r') != std::string_view::npos || name.find('\n') != std::string_view::npos) {
        return "Line breaks not allowed (CRLF injection prevention)";
    }

    // All checks passed
    return "";
}

}  // namespace

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
    // Check middleware chain length limits (DoS prevention)
    for (size_t i = 0; i < config.routes.size(); ++i) {
        const auto& route = config.routes[i];

        if (route.middleware.size() > MAX_MIDDLEWARE_CHAIN_LENGTH) {
            std::ostringstream msg;
            msg << "Route #" << i << " (" << route.path << "): Middleware chain too long ("
                << route.middleware.size() << " > " << MAX_MIDDLEWARE_CHAIN_LENGTH << ")";
            result.add_error(msg.str());
            continue;  // Skip further validation for this route
        }

        // Validate each middleware name for security
        for (const auto& middleware_name : route.middleware) {
            // Security validation (injection, path traversal, DoS)
            std::string security_error = validate_middleware_name_security(middleware_name);
            if (!security_error.empty()) {
                std::ostringstream msg;
                msg << "Route #" << i << " (" << route.path << "): Invalid middleware name '"
                    << middleware_name << "': " << security_error;
                result.add_error(msg.str());
                continue;  // Skip existence check if name is invalid
            }

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

    // Validate named middleware definitions (check all map keys)
    for (const auto& [name, _] : config.rate_limits) {
        std::string security_error = validate_middleware_name_security(name);
        if (!security_error.empty()) {
            result.add_error("Invalid rate_limit name '" + name + "': " + security_error);
        }
    }

    for (const auto& [name, _] : config.cors_configs) {
        std::string security_error = validate_middleware_name_security(name);
        if (!security_error.empty()) {
            result.add_error("Invalid cors_config name '" + name + "': " + security_error);
        }
    }

    for (const auto& [name, _] : config.transform_configs) {
        std::string security_error = validate_middleware_name_security(name);
        if (!security_error.empty()) {
            result.add_error("Invalid transform_config name '" + name + "': " + security_error);
        }
    }

    for (const auto& [name, _] : config.compression_configs) {
        std::string security_error = validate_middleware_name_security(name);
        if (!security_error.empty()) {
            result.add_error("Invalid compression_config name '" + name + "': " + security_error);
        }
    }

    // Check total middleware count (DoS prevention)
    size_t total_middleware = config.rate_limits.size() + config.cors_configs.size() +
                              config.transform_configs.size() + config.compression_configs.size();
    if (total_middleware > MAX_REGISTERED_MIDDLEWARE) {
        std::ostringstream msg;
        msg << "Too many registered middleware (" << total_middleware << " > "
            << MAX_REGISTERED_MIDDLEWARE << ")";
        result.add_error(msg.str());
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
    // Security: Limit typo length to prevent DoS via fuzzy matching
    if (typo.length() > MAX_MIDDLEWARE_NAME_LENGTH) {
        return "";  // Don't suggest for excessively long input
    }

    std::vector<std::string> available = get_all_middleware_names(config);

    // Find similar strings (reduced max distance for security)
    std::vector<std::string> similar =
        titan::core::find_similar_strings(typo, available, MAX_LEVENSHTEIN_DISTANCE);

    // Limit number of suggestions (prevent output bloat)
    if (similar.size() > MAX_FUZZY_MATCH_CANDIDATES) {
        similar.resize(MAX_FUZZY_MATCH_CANDIDATES);
    }

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
