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


// Titan Upstream - Implementation

#include "upstream.hpp"

#include <algorithm>
#include <random>
#include <unistd.h>

namespace titan::gateway {

// Load balancer implementations

Backend* RoundRobinBalancer::select(
    const std::vector<Backend>& backends,
    std::string_view client_ip) {
    (void)client_ip; // Unused

    if (backends.empty()) {
        return nullptr;
    }

    // Filter available backends
    std::vector<Backend*> available;
    for (auto& backend : backends) {
        if (backend.can_accept_connection()) {
            available.push_back(const_cast<Backend*>(&backend));
        }
    }

    if (available.empty()) {
        return nullptr;
    }

    // Round-robin selection
    uint64_t index = counter_.fetch_add(1, std::memory_order_relaxed) % available.size();
    return available[index];
}

Backend* LeastConnectionsBalancer::select(
    const std::vector<Backend>& backends,
    std::string_view client_ip) {
    (void)client_ip; // Unused

    if (backends.empty()) {
        return nullptr;
    }

    // Find backend with least connections
    Backend* selected = nullptr;
    uint32_t min_connections = UINT32_MAX;

    for (auto& backend : backends) {
        if (backend.can_accept_connection() && backend.active_connections < min_connections) {
            min_connections = backend.active_connections;
            selected = const_cast<Backend*>(&backend);
        }
    }

    return selected;
}

Backend* RandomBalancer::select(
    const std::vector<Backend>& backends,
    std::string_view client_ip) {
    (void)client_ip; // Unused

    if (backends.empty()) {
        return nullptr;
    }

    // Filter available backends
    std::vector<Backend*> available;
    for (auto& backend : backends) {
        if (backend.can_accept_connection()) {
            available.push_back(const_cast<Backend*>(&backend));
        }
    }

    if (available.empty()) {
        return nullptr;
    }

    // Random selection
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, available.size() - 1);
    return available[dist(rng)];
}

Backend* WeightedRoundRobinBalancer::select(
    const std::vector<Backend>& backends,
    std::string_view client_ip) {
    (void)client_ip; // Unused

    if (backends.empty()) {
        return nullptr;
    }

    // Build weighted pool: each backend appears N times (N = weight)
    // Example: backend1 (weight=3), backend2 (weight=1) → [b1, b1, b1, b2]
    std::vector<Backend*> weighted_pool;
    uint32_t total_weight = 0;

    for (auto& backend : backends) {
        if (backend.can_accept_connection()) {
            uint32_t weight = backend.weight > 0 ? backend.weight : 1;
            for (uint32_t i = 0; i < weight; ++i) {
                weighted_pool.push_back(const_cast<Backend*>(&backend));
            }
            total_weight += weight;
        }
    }

    if (weighted_pool.empty()) {
        return nullptr;
    }

    // Round-robin selection from weighted pool
    uint64_t index = counter_.fetch_add(1, std::memory_order_relaxed) % weighted_pool.size();
    return weighted_pool[index];
}

// Upstream implementation

Upstream::Upstream(std::string name, size_t backend_pool_size)
    : name_(std::move(name))
    , balancer_(std::make_unique<RoundRobinBalancer>())
    , backend_pool_(backend_pool_size) {}

Upstream::~Upstream() = default;

Upstream::Upstream(Upstream&&) noexcept = default;
Upstream& Upstream::operator=(Upstream&&) noexcept = default;

void Upstream::add_backend(Backend backend) {
    backends_.push_back(std::move(backend));
}

void Upstream::add_backend_with_circuit_breaker(Backend backend, CircuitBreakerConfig cb_config) {
    // Create circuit breaker for this backend
    backend.circuit_breaker = std::make_unique<CircuitBreaker>(cb_config);
    backends_.push_back(std::move(backend));
}

void Upstream::remove_backend(std::string_view address) {
    backends_.erase(
        std::remove_if(backends_.begin(), backends_.end(),
            [address](const Backend& b) { return b.address() == address; }),
        backends_.end());
}

void Upstream::set_load_balancer(std::unique_ptr<LoadBalancer> balancer) {
    balancer_ = std::move(balancer);
}

size_t Upstream::healthy_count() const noexcept {
    return std::count_if(backends_.begin(), backends_.end(),
        [](const Backend& b) { return b.is_available(); });
}

Upstream::Stats Upstream::get_stats() const {
    Stats stats;
    stats.name = name_;
    stats.total_backends = backends_.size();
    stats.healthy_backends = healthy_count();

    for (const auto& backend : backends_) {
        stats.total_requests += backend.total_requests.load(std::memory_order_relaxed);
        stats.total_failures += backend.total_failures.load(std::memory_order_relaxed);
    }

    return stats;
}

// UpstreamManager implementation

void UpstreamManager::register_upstream(std::unique_ptr<Upstream> upstream) {
    upstreams_.push_back(std::move(upstream));
}

Upstream* UpstreamManager::get_upstream(std::string_view name) const {
    for (const auto& upstream : upstreams_) {
        if (upstream->name() == name) {
            return upstream.get();
        }
    }
    return nullptr;
}

void UpstreamManager::remove_upstream(std::string_view name) {
    upstreams_.erase(
        std::remove_if(upstreams_.begin(), upstreams_.end(),
            [name](const std::unique_ptr<Upstream>& u) { return u->name() == name; }),
        upstreams_.end());
}

void UpstreamManager::clear() {
    upstreams_.clear();
}

} // namespace titan::gateway
