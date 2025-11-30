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

// Titan Router - Header
// Radix tree for fast path matching with parameters and wildcards

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../http/http.hpp"

namespace titan::gateway {

/// Route parameter (extracted from path)
struct RouteParam {
    std::string_view name;   // Parameter name (e.g., "id" from /:id)
    std::string_view value;  // Actual value from request path
};

/// Match result from router
struct RouteMatch {
    std::string_view handler_id;     // Unique identifier for matched route
    std::vector<RouteParam> params;  // Extracted path parameters
    std::string_view wildcard;       // Wildcard match (if any)
    std::string_view upstream_name;  // Upstream name for this route

    [[nodiscard]] bool matched() const noexcept { return !handler_id.empty(); }

    // Helper: Get parameter value by name
    [[nodiscard]] std::optional<std::string_view> get_param(std::string_view name) const noexcept;
};

/// Route definition
struct Route {
    std::string path;        // Path pattern (e.g., "/users/:id")
    http::Method method;     // HTTP method (or UNKNOWN for any)
    std::string handler_id;  // Unique handler identifier
    uint32_t priority = 0;   // Higher priority = checked first

    // Backend configuration (for proxy routes)
    std::string upstream_name;  // Name of upstream group
    std::string rewrite_path;   // Optional path rewriting
};

/// Radix tree node (internal)
class RadixNode {
public:
    RadixNode() = default;
    ~RadixNode() = default;

    // Non-copyable, movable
    RadixNode(const RadixNode&) = delete;
    RadixNode& operator=(const RadixNode&) = delete;
    RadixNode(RadixNode&&) noexcept = default;
    RadixNode& operator=(RadixNode&&) noexcept = default;

    std::string prefix;                                // Path prefix for this node
    std::unordered_map<http::Method, Route> handlers;  // Method -> Route mapping
    std::vector<std::unique_ptr<RadixNode>> children;  // Child nodes

    bool is_param = false;     // True if this is a :param node
    bool is_wildcard = false;  // True if this is a * wildcard node
    std::string param_name;    // Parameter name (if is_param)
};

/// Router with Radix tree for path matching
class Router {
public:
    Router();
    ~Router();

    // Non-copyable, movable
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) noexcept;
    Router& operator=(Router&&) noexcept;

    /// Add a route to the router
    void add_route(Route route);

    /// Find matching route for given method and path
    [[nodiscard]] RouteMatch match(http::Method method, std::string_view path) const;

    /// Get all registered routes
    [[nodiscard]] const std::vector<Route>& routes() const noexcept { return routes_; }

    /// Clear all routes
    void clear();

    /// Get statistics
    struct Stats {
        size_t total_routes = 0;
        size_t total_nodes = 0;
        size_t max_depth = 0;
    };
    [[nodiscard]] Stats get_stats() const;

private:
    // Insert route into radix tree
    void insert_route(const Route& route);

    // Search radix tree for match
    RouteMatch search(RadixNode* node, std::string_view path, http::Method method,
                      std::vector<RouteParam>& params, size_t depth = 0) const;

    // Find common prefix length between two strings
    static size_t common_prefix_length(std::string_view a, std::string_view b);

    // Split node at given position
    RadixNode* split_node(RadixNode* node, size_t pos);

    // Calculate tree statistics
    void calculate_stats(const RadixNode* node, Stats& stats, size_t depth) const;

    std::unique_ptr<RadixNode> root_;
    std::vector<Route> routes_;  // Keep track of all routes for inspection
};

/// Route builder (fluent API)
class RouteBuilder {
public:
    explicit RouteBuilder(std::string path) {
        route_.path = std::move(path);
        route_.method = http::Method::UNKNOWN;
    }

    RouteBuilder& method(http::Method m) {
        route_.method = m;
        return *this;
    }

    RouteBuilder& handler(std::string id) {
        route_.handler_id = std::move(id);
        return *this;
    }

    RouteBuilder& upstream(std::string name) {
        route_.upstream_name = std::move(name);
        return *this;
    }

    RouteBuilder& rewrite(std::string path) {
        route_.rewrite_path = std::move(path);
        return *this;
    }

    RouteBuilder& priority(uint32_t p) {
        route_.priority = p;
        return *this;
    }

    Route build() && { return std::move(route_); }

private:
    Route route_;
};

// Helper functions

/// Parse path parameters from pattern (e.g., "/users/:id" -> ["id"])
[[nodiscard]] std::vector<std::string> extract_param_names(std::string_view pattern);

/// Check if path segment is a parameter (starts with :)
[[nodiscard]] inline bool is_param_segment(std::string_view segment) {
    return !segment.empty() && segment[0] == ':';
}

/// Check if path segment is a wildcard (is *)
[[nodiscard]] inline bool is_wildcard_segment(std::string_view segment) {
    return segment == "*";
}

}  // namespace titan::gateway
