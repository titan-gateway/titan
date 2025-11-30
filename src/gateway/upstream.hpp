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

// Titan Upstream - Header
// Connection pooling and load balancing for backend servers

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "circuit_breaker.hpp"
#include "connection_pool.hpp"

namespace titan::gateway {

/// Backend server status
enum class BackendStatus : uint8_t {
    Healthy,
    Degraded,
    Unhealthy,
    Draining  // Graceful shutdown
};

/// Backend server definition
struct Backend {
    std::string host;
    uint16_t port = 80;
    uint32_t weight = 1;  // For weighted load balancing
    BackendStatus status = BackendStatus::Healthy;

    // Connection limits
    uint32_t max_connections = 1000;
    uint32_t active_connections = 0;

    // Health check
    std::chrono::steady_clock::time_point last_health_check;
    uint32_t consecutive_failures = 0;

    // Circuit breaker
    std::unique_ptr<CircuitBreaker> circuit_breaker;

    // Statistics
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_failures{0};
    std::atomic<uint64_t> total_bytes_sent{0};
    std::atomic<uint64_t> total_bytes_received{0};

    // Make Backend movable by handling atomics
    Backend() = default;
    Backend(Backend&& other) noexcept
        : host(std::move(other.host)),
          port(other.port),
          weight(other.weight),
          status(other.status),
          max_connections(other.max_connections),
          active_connections(other.active_connections),
          last_health_check(other.last_health_check),
          consecutive_failures(other.consecutive_failures),
          circuit_breaker(std::move(other.circuit_breaker)),
          total_requests(other.total_requests.load()),
          total_failures(other.total_failures.load()),
          total_bytes_sent(other.total_bytes_sent.load()),
          total_bytes_received(other.total_bytes_received.load()) {}

    Backend& operator=(Backend&& other) noexcept {
        if (this != &other) {
            host = std::move(other.host);
            port = other.port;
            weight = other.weight;
            status = other.status;
            max_connections = other.max_connections;
            active_connections = other.active_connections;
            last_health_check = other.last_health_check;
            consecutive_failures = other.consecutive_failures;
            circuit_breaker = std::move(other.circuit_breaker);
            total_requests.store(other.total_requests.load());
            total_failures.store(other.total_failures.load());
            total_bytes_sent.store(other.total_bytes_sent.load());
            total_bytes_received.store(other.total_bytes_received.load());
        }
        return *this;
    }

    // Delete copy operations
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] bool is_available() const noexcept {
        return status == BackendStatus::Healthy || status == BackendStatus::Degraded;
    }

    [[nodiscard]] bool can_accept_connection() const noexcept {
        // Gate 1: Health check status (background validation)
        if (!is_available()) {
            return false;  // HealthChecker marked backend as down
        }

        // Gate 2: Circuit breaker state (real-time failure tracking)
        // Note: const_cast is safe here since should_allow_request() only reads state
        // for CLOSED/OPEN decision (mutations happen in record_failure/success)
        if (circuit_breaker &&
            !const_cast<CircuitBreaker*>(circuit_breaker.get())->should_allow_request()) {
            return false;  // Too many recent failures, circuit is OPEN
        }

        // Gate 3: Connection limit
        if (active_connections >= max_connections) {
            return false;
        }

        return true;
    }

    [[nodiscard]] std::string address() const { return host + ":" + std::to_string(port); }
};

/// Load balancing strategy
enum class LoadBalancingStrategy : uint8_t {
    RoundRobin,          // Simple round-robin
    LeastConnections,    // Pick backend with fewest connections
    Random,              // Random selection
    WeightedRoundRobin,  // Round-robin with weights
    IPHash               // Hash based on client IP (sticky sessions)
};

/// Load balancer interface
class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;

    /// Select backend for new request
    [[nodiscard]] virtual Backend* select(const std::vector<Backend>& backends,
                                          std::string_view client_ip = {}) = 0;

    /// Notify balancer of successful request
    virtual void on_success(Backend* backend) {}

    /// Notify balancer of failed request
    virtual void on_failure(Backend* backend) {}
};

/// Round-robin load balancer
class RoundRobinBalancer : public LoadBalancer {
public:
    Backend* select(const std::vector<Backend>& backends, std::string_view client_ip) override;

private:
    std::atomic<uint64_t> counter_{0};
};

/// Least connections load balancer
class LeastConnectionsBalancer : public LoadBalancer {
public:
    Backend* select(const std::vector<Backend>& backends, std::string_view client_ip) override;
};

/// Random load balancer
class RandomBalancer : public LoadBalancer {
public:
    Backend* select(const std::vector<Backend>& backends, std::string_view client_ip) override;
};

/// Weighted round-robin load balancer
/// Distributes requests based on backend weights (higher weight = more requests)
/// Algorithm: Each backend appears in selection pool N times (N = weight)
class WeightedRoundRobinBalancer : public LoadBalancer {
public:
    Backend* select(const std::vector<Backend>& backends, std::string_view client_ip) override;

private:
    std::atomic<uint64_t> counter_{0};
};

/// Upstream group (multiple backends with load balancing)
class Upstream {
public:
    explicit Upstream(std::string name, size_t backend_pool_size = 64);
    ~Upstream();

    // Non-copyable, movable
    Upstream(const Upstream&) = delete;
    Upstream& operator=(const Upstream&) = delete;
    Upstream(Upstream&&) noexcept;
    Upstream& operator=(Upstream&&) noexcept;

    /// Add backend to upstream
    void add_backend(Backend backend);

    /// Add backend with circuit breaker
    void add_backend_with_circuit_breaker(Backend backend, CircuitBreakerConfig cb_config);

    /// Remove backend by address
    void remove_backend(std::string_view address);

    /// Set load balancing strategy
    void set_load_balancer(std::unique_ptr<LoadBalancer> balancer);

    /// Get upstream name
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    /// Get all backends
    [[nodiscard]] const std::vector<Backend>& backends() const noexcept { return backends_; }

    /// Get healthy backend count
    [[nodiscard]] size_t healthy_count() const noexcept;

    /// Get upstream statistics
    struct Stats {
        std::string name;
        size_t total_backends = 0;
        size_t healthy_backends = 0;
        uint64_t total_requests = 0;
        uint64_t total_failures = 0;
    };
    [[nodiscard]] Stats get_stats() const;

    /// Get backend connection pool
    [[nodiscard]] BackendConnectionPool& backend_pool() noexcept { return backend_pool_; }
    [[nodiscard]] const BackendConnectionPool& backend_pool() const noexcept {
        return backend_pool_;
    }

private:
    std::string name_;
    std::vector<Backend> backends_;
    std::unique_ptr<LoadBalancer> balancer_;
    BackendConnectionPool backend_pool_;  // Simple FD-based pool for async backend
};

/// Upstream manager (registry of all upstreams)
class UpstreamManager {
public:
    UpstreamManager() = default;
    ~UpstreamManager() = default;

    // Non-copyable, non-movable (singleton-like)
    UpstreamManager(const UpstreamManager&) = delete;
    UpstreamManager& operator=(const UpstreamManager&) = delete;

    /// Register upstream
    void register_upstream(std::unique_ptr<Upstream> upstream);

    /// Get upstream by name
    [[nodiscard]] Upstream* get_upstream(std::string_view name) const;

    /// Get all upstreams
    [[nodiscard]] const std::vector<std::unique_ptr<Upstream>>& upstreams() const noexcept {
        return upstreams_;
    }

    /// Remove upstream by name
    void remove_upstream(std::string_view name);

    /// Clear all upstreams
    void clear();

private:
    std::vector<std::unique_ptr<Upstream>> upstreams_;
};

}  // namespace titan::gateway
