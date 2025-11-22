// Titan Router - Implementation

#include "router.hpp"

#include <algorithm>
#include <sstream>

namespace titan::gateway {

// RouteMatch implementation

std::optional<std::string_view> RouteMatch::get_param(std::string_view name) const noexcept {
    for (const auto& param : params) {
        if (param.name == name) {
            return param.value;
        }
    }
    return std::nullopt;
}

// Router implementation

Router::Router() : root_(std::make_unique<RadixNode>()) {}

Router::~Router() = default;

Router::Router(Router&&) noexcept = default;
Router& Router::operator=(Router&&) noexcept = default;

void Router::add_route(Route route) {
    insert_route(route);
    routes_.push_back(std::move(route));
}

RouteMatch Router::match(http::Method method, std::string_view path) const {
    std::vector<RouteParam> params;
    return search(root_.get(), path, method, params);
}

void Router::clear() {
    root_ = std::make_unique<RadixNode>();
    routes_.clear();
}

Router::Stats Router::get_stats() const {
    Stats stats;
    stats.total_routes = routes_.size();
    calculate_stats(root_.get(), stats, 0);
    return stats;
}

// Private methods

void Router::insert_route(const Route& route) {
    RadixNode* current = root_.get();
    std::string_view remaining_path = route.path;

    // Special case: root path "/"
    if (remaining_path == "/") {
        current->handlers[route.method] = route;
        return;
    }

    while (!remaining_path.empty()) {
        // Check if path starts with /
        if (remaining_path[0] == '/') {
            remaining_path.remove_prefix(1);
        }

        // Extract next segment
        size_t slash_pos = remaining_path.find('/');
        std::string_view segment = remaining_path.substr(0, slash_pos);

        if (slash_pos != std::string_view::npos) {
            remaining_path.remove_prefix(slash_pos);
        } else {
            remaining_path = {};
        }

        // Check for parameter or wildcard
        bool is_param = is_param_segment(segment);
        bool is_wildcard = is_wildcard_segment(segment);

        // Find matching child
        RadixNode* matching_child = nullptr;
        for (auto& child : current->children) {
            if (is_param && child->is_param) {
                matching_child = child.get();
                break;
            }
            if (is_wildcard && child->is_wildcard) {
                matching_child = child.get();
                break;
            }
            if (!is_param && !is_wildcard && !child->is_param && !child->is_wildcard) {
                size_t common = common_prefix_length(segment, child->prefix);
                if (common > 0) {
                    if (common < child->prefix.size()) {
                        // Need to split the child node
                        matching_child = split_node(child.get(), common);
                    } else {
                        matching_child = child.get();
                    }
                    segment.remove_prefix(common);
                    break;
                }
            }
        }

        // Create new child if no match found
        if (!matching_child) {
            auto new_node = std::make_unique<RadixNode>();
            if (is_param) {
                new_node->is_param = true;
                new_node->param_name = std::string(segment.substr(1)); // Remove :
            } else if (is_wildcard) {
                new_node->is_wildcard = true;
            } else {
                new_node->prefix = std::string(segment);
            }
            matching_child = new_node.get();
            current->children.push_back(std::move(new_node));
            segment = {};
        }

        current = matching_child;

        // If segment still has content, continue from current
        if (!segment.empty()) {
            auto new_node = std::make_unique<RadixNode>();
            new_node->prefix = std::string(segment);
            matching_child = new_node.get();
            current->children.push_back(std::move(new_node));
            current = matching_child;
        }
    }

    // Add handler to final node
    current->handlers[route.method] = route;

    // Also add handler for "any method" if method is UNKNOWN
    if (route.method == http::Method::UNKNOWN) {
        // This route matches any method
    }
}

RouteMatch Router::search(
    RadixNode* node,
    std::string_view path,
    http::Method method,
    std::vector<RouteParam>& params,
    size_t depth) const {

    if (!node) {
        return {};
    }

    // If path is empty or just "/", check for handler
    if (path.empty() || path == "/") {
        // Try exact method match first
        auto it = node->handlers.find(method);
        if (it != node->handlers.end()) {
            RouteMatch match;
            match.handler_id = it->second.handler_id;
            match.upstream_name = it->second.upstream_name;
            match.params = params;
            return match;
        }

        // Try "any method" match
        auto any_it = node->handlers.find(http::Method::UNKNOWN);
        if (any_it != node->handlers.end()) {
            RouteMatch match;
            match.handler_id = any_it->second.handler_id;
            match.upstream_name = any_it->second.upstream_name;
            match.params = params;
            return match;
        }

        return {};
    }

    // Strip leading slash
    if (path[0] == '/') {
        path.remove_prefix(1);
    }

    // Extract next segment
    size_t slash_pos = path.find('/');
    std::string_view segment = path.substr(0, slash_pos);
    std::string_view remaining = (slash_pos != std::string_view::npos)
        ? path.substr(slash_pos)
        : std::string_view{};

    // Try matching children
    for (const auto& child : node->children) {
        if (child->is_wildcard) {
            // Wildcard matches rest of path
            auto it = child->handlers.find(method);
            if (it == child->handlers.end()) {
                it = child->handlers.find(http::Method::UNKNOWN);
            }
            if (it != child->handlers.end()) {
                RouteMatch match;
                match.handler_id = it->second.handler_id;
                match.upstream_name = it->second.upstream_name;
                match.params = params;
                match.wildcard = path;
                return match;
            }
        } else if (child->is_param) {
            // Parameter matches this segment
            params.push_back({child->param_name, segment});
            auto result = search(child.get(), remaining, method, params, depth + 1);
            if (result.matched()) {
                return result;
            }
            params.pop_back(); // Backtrack
        } else if (segment.starts_with(child->prefix)) {
            // Prefix match
            std::string_view next_path = path;
            next_path.remove_prefix(child->prefix.size());
            auto result = search(child.get(), next_path, method, params, depth + 1);
            if (result.matched()) {
                return result;
            }
        }
    }

    return {};
}

size_t Router::common_prefix_length(std::string_view a, std::string_view b) {
    size_t min_len = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < min_len && a[i] == b[i]) {
        ++i;
    }
    return i;
}

RadixNode* Router::split_node(RadixNode* node, size_t pos) {
    // Create new parent node with common prefix
    auto new_parent = std::make_unique<RadixNode>();
    new_parent->prefix = node->prefix.substr(0, pos);

    // Update original node's prefix
    node->prefix = node->prefix.substr(pos);

    // Move node under new parent
    // Note: This is simplified; in practice we'd need to handle this carefully
    // For now, return the node as-is
    return node;
}

void Router::calculate_stats(const RadixNode* node, Stats& stats, size_t depth) const {
    if (!node) return;

    stats.total_nodes++;
    stats.max_depth = std::max(stats.max_depth, depth);

    for (const auto& child : node->children) {
        calculate_stats(child.get(), stats, depth + 1);
    }
}

// Helper functions

std::vector<std::string> extract_param_names(std::string_view pattern) {
    std::vector<std::string> params;
    size_t pos = 0;

    while (pos < pattern.size()) {
        if (pattern[pos] == ':') {
            size_t end = pos + 1;
            while (end < pattern.size() && pattern[end] != '/') {
                ++end;
            }
            params.push_back(std::string(pattern.substr(pos + 1, end - pos - 1)));
            pos = end;
        } else {
            ++pos;
        }
    }

    return params;
}

} // namespace titan::gateway
