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

namespace titan::gateway {

/// Backend server status
enum class BackendStatus : uint8_t {
    Healthy,
    Degraded,
    Unhealthy,
    Draining    // Graceful shutdown
};

/// Backend server definition
struct Backend {
    std::string host;
    uint16_t port = 80;
    uint32_t weight = 1;                   // For weighted load balancing
    BackendStatus status = BackendStatus::Healthy;

    // Connection limits
    uint32_t max_connections = 1000;
    uint32_t active_connections = 0;

    // Health check
    std::chrono::steady_clock::time_point last_health_check;
    uint32_t consecutive_failures = 0;

    // Statistics
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_failures{0};
    std::atomic<uint64_t> total_bytes_sent{0};
    std::atomic<uint64_t> total_bytes_received{0};

    // Make Backend movable by handling atomics
    Backend() = default;
    Backend(Backend&& other) noexcept
        : host(std::move(other.host))
        , port(other.port)
        , weight(other.weight)
        , status(other.status)
        , max_connections(other.max_connections)
        , active_connections(other.active_connections)
        , last_health_check(other.last_health_check)
        , consecutive_failures(other.consecutive_failures)
        , total_requests(other.total_requests.load())
        , total_failures(other.total_failures.load())
        , total_bytes_sent(other.total_bytes_sent.load())
        , total_bytes_received(other.total_bytes_received.load()) {}

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
        return is_available() && active_connections < max_connections;
    }

    [[nodiscard]] std::string address() const {
        return host + ":" + std::to_string(port);
    }
};

/// Load balancing strategy
enum class LoadBalancingStrategy : uint8_t {
    RoundRobin,         // Simple round-robin
    LeastConnections,   // Pick backend with fewest connections
    Random,             // Random selection
    WeightedRoundRobin, // Round-robin with weights
    IPHash              // Hash based on client IP (sticky sessions)
};

/// Connection to backend
struct Connection {
    int sockfd = -1;
    Backend* backend = nullptr;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_used;
    uint64_t requests_served = 0;
    bool keep_alive = true;

    [[nodiscard]] bool is_valid() const noexcept {
        return sockfd >= 0 && backend != nullptr;
    }

    [[nodiscard]] bool is_expired(std::chrono::seconds max_idle) const noexcept {
        auto now = std::chrono::steady_clock::now();
        return (now - last_used) > max_idle;
    }

    [[nodiscard]] bool is_old(std::chrono::seconds max_lifetime) const noexcept {
        auto now = std::chrono::steady_clock::now();
        return (now - created_at) > max_lifetime;
    }
};

/// Connection pool (per-thread, LIFO for cache locality)
class ConnectionPool {
public:
    explicit ConnectionPool(size_t max_size = 100);
    ~ConnectionPool();

    // Non-copyable, movable
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;
    ConnectionPool(ConnectionPool&&) noexcept;
    ConnectionPool& operator=(ConnectionPool&&) noexcept;

    /// Acquire connection (from pool or create new)
    [[nodiscard]] Connection* acquire(Backend* backend);

    /// Release connection back to pool
    void release(Connection* conn);

    /// Close connection
    void close(Connection* conn);

    /// Evict expired connections
    void evict_expired(std::chrono::seconds max_idle);

    /// Get pool statistics
    struct Stats {
        size_t total_connections = 0;
        size_t idle_connections = 0;
        size_t active_connections = 0;
        uint64_t total_acquires = 0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
    };
    [[nodiscard]] Stats get_stats() const noexcept { return stats_; }

private:
    std::vector<std::unique_ptr<Connection>> pool_;
    std::vector<Connection*> free_list_;  // LIFO stack of available connections
    size_t max_size_;
    Stats stats_;
};

/// Load balancer interface
class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;

    /// Select backend for new request
    [[nodiscard]] virtual Backend* select(
        const std::vector<Backend>& backends,
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

/// Upstream group (multiple backends with load balancing)
class Upstream {
public:
    explicit Upstream(std::string name);
    ~Upstream();

    // Non-copyable, movable
    Upstream(const Upstream&) = delete;
    Upstream& operator=(const Upstream&) = delete;
    Upstream(Upstream&&) noexcept;
    Upstream& operator=(Upstream&&) noexcept;

    /// Add backend to upstream
    void add_backend(Backend backend);

    /// Remove backend by address
    void remove_backend(std::string_view address);

    /// Get connection to backend (selects backend using load balancer)
    [[nodiscard]] Connection* get_connection(std::string_view client_ip = {});

    /// Release connection back to pool
    void release_connection(Connection* conn);

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
        ConnectionPool::Stats pool_stats;
    };
    [[nodiscard]] Stats get_stats() const;

private:
    std::string name_;
    std::vector<Backend> backends_;
    std::unique_ptr<LoadBalancer> balancer_;
    ConnectionPool pool_;
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

} // namespace titan::gateway
