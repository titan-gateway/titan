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

// Configuration Validator - Security & Correctness Validation

#pragma once

#include <string>
#include <vector>

#include "config.hpp"

namespace titan::control {

/// Enhanced validation for security policies and typo detection
class ConfigValidator {
public:
    /// Validate configuration with strict security checks
    [[nodiscard]] static ValidationResult validate(const Config& config);

private:
    /// Validate middleware references (detect typos)
    static void validate_middleware_references(const Config& config, ValidationResult& result);

    /// Validate security policies (auth requirements, rate limit weakening)
    static void validate_security_policies(const Config& config, ValidationResult& result);

    /// Validate for duplicate middleware of same type
    static void validate_middleware_duplicates(const Config& config, ValidationResult& result);

    /// Check if middleware name exists in any middleware pool
    [[nodiscard]] static bool middleware_exists(const Config& config, const std::string& name);

    /// Get all available middleware names
    [[nodiscard]] static std::vector<std::string> get_all_middleware_names(const Config& config);

    /// Suggest similar middleware names for typos
    [[nodiscard]] static std::string suggest_similar_middleware(const Config& config,
                                                                const std::string& typo);
};

}  // namespace titan::control
