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

// Transform Middleware - Request/Response transformation
// Supports header manipulation, path rewriting (prefix strip + regex), query parameter manipulation

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../control/config.hpp"
#include "../core/containers.hpp"
#include "../http/regex.hpp"
#include "pipeline.hpp"

namespace titan::gateway {

/// Transform Middleware - Apply request/response transformations
/// Execution order: After Auth, Before Proxy (Phase 1 for request, Phase 2 for response)
class TransformMiddleware : public Middleware {
public:
    explicit TransformMiddleware(control::TransformConfig global_config);

    // Two-Phase Middleware interface
    [[nodiscard]] MiddlewareResult process_request(RequestContext& ctx) override;
    [[nodiscard]] MiddlewareResult process_response(ResponseContext& ctx) override;

    [[nodiscard]] std::string_view name() const override { return "transform"; }

private:
    // Configuration (global, can be overridden per-route)
    control::TransformConfig global_config_;

    // Compiled regex cache (thread-local, lazy compilation)
    // Maps pattern → compiled regex
    mutable titan::core::fast_map<std::string, titan::http::Regex> regex_cache_;

    // Helper: Get compiled regex (with caching)
    [[nodiscard]] const titan::http::Regex* get_compiled_regex(const std::string& pattern) const;

    // Helper: Merge global and per-route configs (per-route overrides global)
    [[nodiscard]] control::TransformConfig merge_configs(
        const std::optional<control::TransformConfig>& route_config) const;

    // Path transformation
    [[nodiscard]] std::optional<std::string> apply_path_rewrites(
        std::string_view path, const std::vector<control::PathRewriteRule>& rules) const;

    // Query parameter transformation
    struct QueryParams {
        std::vector<std::pair<std::string, std::string>> params;
    };

    [[nodiscard]] QueryParams parse_query(std::string_view query) const;
    [[nodiscard]] std::string build_query(const QueryParams& params) const;
    void apply_query_transformations(QueryParams& params,
                                     const std::vector<control::QueryRule>& rules) const;

    // Header transformation (for request)
    void apply_request_header_transformations(RequestContext& ctx,
                                              const std::vector<control::HeaderRule>& rules);

    // Header transformation (for response)
    void apply_response_header_transformations(ResponseContext& ctx,
                                               const std::vector<control::HeaderRule>& rules);
};

}  // namespace titan::gateway
