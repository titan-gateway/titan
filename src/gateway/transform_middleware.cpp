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

// Transform Middleware - Implementation

#include "transform_middleware.hpp"

#include <algorithm>
#include <sstream>

namespace titan::gateway {

TransformMiddleware::TransformMiddleware(control::TransformConfig global_config)
    : global_config_(std::move(global_config)) {}

MiddlewareResult TransformMiddleware::process_request(RequestContext& ctx) {
    // Merge global and per-route configs (per-route overrides global)
    // Make a deep copy to ensure proper string lifetime
    // This is NOT a hot path (only happens once per request during middleware setup)
    // Safety > Performance for config handling
    control::TransformConfig active_config;
    if (ctx.route_match.transform_config.has_value()) {
        // Per-route config exists - make a full deep copy (all strings are now owned)
        active_config = *ctx.route_match.transform_config;
    } else {
        // Use global config - make a copy
        active_config = global_config_;
    }

    if (!active_config.enabled) {
        return MiddlewareResult::Continue;  // Transformations disabled
    }

    // 1. Apply path transformations
    if (!active_config.path_rewrites.empty()) {
        auto transformed_path = apply_path_rewrites(ctx.request->path, active_config.path_rewrites);
        if (transformed_path.has_value()) {
            // Store transformed path in metadata for proxy to use
            ctx.set_metadata("transformed_path", std::move(*transformed_path));
        }
    }

    // 2. Apply query parameter transformations
    if (!active_config.query_params.empty() && !ctx.request->query.empty()) {
        auto params = parse_query(ctx.request->query);
        apply_query_transformations(params, active_config.query_params);
        auto transformed_query = build_query(params);

        // Store transformed query in metadata
        ctx.set_metadata("transformed_query", std::move(transformed_query));
    }

    // 3. Apply request header transformations
    if (!active_config.request_headers.empty()) {
        apply_request_header_transformations(ctx, active_config.request_headers);
    }

    // 4. Store response header transformations in metadata for response phase
    // NOTE: active_config is stack-local and destroyed after this function returns!
    // Must explicitly copy string values to metadata, not just reference them.
    if (!active_config.response_headers.empty()) {
        for (const auto& rule : active_config.response_headers) {
            std::string key = "response_header_" + rule.action + ":" + rule.name;
            // CRITICAL: Explicitly create owned copy - active_config is destroyed when function
            // returns
            ctx.set_metadata(std::move(key), std::string(rule.value));
        }
    }

    return MiddlewareResult::Continue;
}

MiddlewareResult TransformMiddleware::process_response(ResponseContext& ctx) {
    // Check if response header transformations were stored in metadata from request phase
    bool has_metadata_rules = false;
    for (const auto& [key, value] : ctx.metadata) {
        if (key.starts_with("response_header_")) {
            has_metadata_rules = true;
            break;
        }
    }

    if (has_metadata_rules) {
        // Apply response header transformations from metadata (normal two-phase flow)
        // Metadata keys: "response_header_add:X-Custom", "response_header_remove:Server", etc.

        // IMPORTANT: Collect all header operations first to avoid iterator invalidation
        // (ctx.set_metadata() modifies the map we're iterating over)
        std::vector<std::pair<std::string, std::string>> headers_to_add;
        std::vector<std::string> headers_to_remove;
        std::vector<std::pair<std::string, std::string>> headers_to_modify;

        for (const auto& [key, value] : ctx.metadata) {
            if (key.starts_with("response_header_add:")) {
                std::string header_name = key.substr(20);  // Skip "response_header_add:"
                headers_to_add.emplace_back(std::move(header_name), std::string(value));
            } else if (key.starts_with("response_header_remove:")) {
                std::string header_name = key.substr(23);  // Skip "response_header_remove:"
                headers_to_remove.emplace_back(std::move(header_name));
            } else if (key.starts_with("response_header_modify:")) {
                std::string header_name = key.substr(23);  // Skip "response_header_modify:"
                headers_to_modify.emplace_back(std::move(header_name), std::string(value));
            }
        }

        // Now apply collected operations (safe - not iterating anymore)
        for (auto& [name, value] : headers_to_add) {
            // Hybrid storage: add_middleware_header() copies to owned strings
            // Completely safe - no need for metadata storage anymore!
            ctx.response->add_middleware_header(name, value);
        }

        for (const auto& name : headers_to_remove) {
            ctx.response->remove_header(name);
        }

        for (auto& [name, value] : headers_to_modify) {
            if (!ctx.response->modify_header(name, value)) {
                // Header doesn't exist, add it using hybrid storage
                ctx.response->add_middleware_header(name, value);
            }
        }
    } else {
        // No metadata rules - apply global config directly (for tests or legacy usage)
        apply_response_header_transformations(ctx, global_config_.response_headers);
    }

    return MiddlewareResult::Continue;
}

// Helper: Get compiled regex with caching
const titan::http::Regex* TransformMiddleware::get_compiled_regex(
    const std::string& pattern) const {
    // Check cache first
    auto it = regex_cache_.find(pattern);
    if (it != regex_cache_.end()) {
        return &it->second;
    }

    // Compile and cache
    auto regex = titan::http::Regex::compile(pattern);
    if (!regex.has_value()) {
        return nullptr;  // Compilation failed (should have been caught in validation)
    }

    auto [iter, inserted] = regex_cache_.emplace(pattern, std::move(*regex));
    return &iter->second;
}

// Helper: Merge global and per-route configs
control::TransformConfig TransformMiddleware::merge_configs(
    const std::optional<control::TransformConfig>& route_config) const {
    if (!route_config.has_value()) {
        return global_config_;  // No per-route config, use global
    }

    if (!route_config->enabled) {
        return *route_config;  // Per-route explicitly disabled
    }

    // Per-route config enabled - use it (overrides global)
    return *route_config;
}

// Path transformation
std::optional<std::string> TransformMiddleware::apply_path_rewrites(
    std::string_view path, const std::vector<control::PathRewriteRule>& rules) const {
    std::string current_path(path);

    for (const auto& rule : rules) {
        if (rule.type == "prefix_strip") {
            // Strip prefix if path starts with pattern
            if (current_path.starts_with(rule.pattern)) {
                current_path = current_path.substr(rule.pattern.size());

                // Ensure path starts with /
                if (current_path.empty() || current_path[0] != '/') {
                    current_path = "/" + current_path;
                }
            }
        } else if (rule.type == "regex") {
            // Apply regex substitution
            const auto* regex = get_compiled_regex(rule.pattern);
            if (regex) {
                auto substituted = regex->substitute(current_path, rule.replacement);
                if (substituted != current_path) {
                    current_path = std::move(substituted);
                }
            }
        }
    }

    // Return transformed path if it changed
    if (current_path != path) {
        return current_path;
    }

    return std::nullopt;  // No transformation applied
}

// Query parameter parsing
TransformMiddleware::QueryParams TransformMiddleware::parse_query(std::string_view query) const {
    QueryParams result;

    if (query.empty()) {
        return result;
    }

    // Split by & to get key=value pairs
    size_t start = 0;
    while (start < query.size()) {
        size_t amp_pos = query.find('&', start);
        std::string_view pair = (amp_pos == std::string_view::npos)
                                    ? query.substr(start)
                                    : query.substr(start, amp_pos - start);

        // Split by = to get key and value
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string_view::npos) {
            std::string key(pair.substr(0, eq_pos));
            std::string value(pair.substr(eq_pos + 1));

            // URL decode key and value
            auto decoded_key = titan::http::url::decode(key);
            auto decoded_value = titan::http::url::decode(value);

            if (decoded_key.has_value() && decoded_value.has_value()) {
                result.params.emplace_back(std::move(*decoded_key), std::move(*decoded_value));
            } else {
                // Invalid encoding, keep original
                result.params.emplace_back(std::move(key), std::move(value));
            }
        } else {
            // No value (e.g., "?flag")
            auto decoded_key = titan::http::url::decode(std::string(pair));
            result.params.emplace_back(decoded_key.value_or(std::string(pair)), "");
        }

        if (amp_pos == std::string_view::npos) {
            break;
        }
        start = amp_pos + 1;
    }

    return result;
}

// Query parameter building
std::string TransformMiddleware::build_query(const QueryParams& params) const {
    if (params.params.empty()) {
        return "";
    }

    std::ostringstream oss;
    bool first = true;

    for (const auto& [key, value] : params.params) {
        if (!first) {
            oss << '&';
        }
        first = false;

        oss << titan::http::url::encode(key);
        if (!value.empty()) {
            oss << '=' << titan::http::url::encode(value);
        }
    }

    return oss.str();
}

// Query parameter transformations
void TransformMiddleware::apply_query_transformations(
    QueryParams& params, const std::vector<control::QueryRule>& rules) const {
    for (const auto& rule : rules) {
        if (rule.action == "remove") {
            // Remove all parameters with matching name
            params.params.erase(
                std::remove_if(params.params.begin(), params.params.end(),
                               [&rule](const auto& p) { return p.first == rule.name; }),
                params.params.end());
        } else if (rule.action == "modify") {
            // Modify first matching parameter
            for (auto& [key, value] : params.params) {
                if (key == rule.name) {
                    value = rule.value;
                    break;  // Only modify first occurrence
                }
            }
        } else if (rule.action == "add") {
            // Add new parameter (at the end)
            params.params.emplace_back(rule.name, rule.value);
        }
    }
}

// Request header transformations
void TransformMiddleware::apply_request_header_transformations(
    RequestContext& ctx, const std::vector<control::HeaderRule>& rules) {
    // Note: We cannot directly modify ctx.request->headers (it's a view into the buffer)
    // Instead, we store header transformations in metadata for the proxy to apply
    // This is a design limitation - headers are zero-copy views

    // For now, we'll track header modifications in metadata
    // Format: "header_add:<name>" → value, "header_remove:<name>" → "", "header_modify:<name>" →
    // value

    for (const auto& rule : rules) {
        if (rule.action == "add") {
            ctx.set_metadata("header_add:" + rule.name, rule.value);
        } else if (rule.action == "remove") {
            ctx.set_metadata("header_remove:" + rule.name, "true");
        } else if (rule.action == "modify") {
            ctx.set_metadata("header_modify:" + rule.name, rule.value);
        }
    }
}

// Response header transformations
void TransformMiddleware::apply_response_header_transformations(
    ResponseContext& ctx, const std::vector<control::HeaderRule>& rules) {
    for (const auto& rule : rules) {
        if (rule.action == "remove") {
            ctx.response->remove_header(rule.name);
        } else if (rule.action == "modify") {
            // Modify existing header (or add if doesn't exist)
            if (!ctx.response->modify_header(rule.name, rule.value)) {
                // Header doesn't exist, add it using hybrid storage
                ctx.response->add_middleware_header(rule.name, rule.value);
            }
        } else if (rule.action == "add") {
            // Hybrid storage: add_middleware_header() copies to owned strings
            ctx.response->add_middleware_header(rule.name, rule.value);
        }
    }
}

}  // namespace titan::gateway
