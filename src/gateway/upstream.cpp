// Titan Upstream - Implementation

#include "upstream.hpp"

#include <algorithm>
#include <random>
#include <unistd.h>

namespace titan::gateway {

// ConnectionPool implementation

ConnectionPool::ConnectionPool(size_t max_size)
    : max_size_(max_size) {
    pool_.reserve(max_size);
}

ConnectionPool::~ConnectionPool() {
    // Close all connections
    for (auto& conn : pool_) {
        if (conn && conn->is_valid()) {
            ::close(conn->sockfd);
        }
    }
}

ConnectionPool::ConnectionPool(ConnectionPool&&) noexcept = default;
ConnectionPool& ConnectionPool::operator=(ConnectionPool&&) noexcept = default;

Connection* ConnectionPool::acquire(Backend* backend) {
    stats_.total_acquires++;

    // Try to get from free list (LIFO for cache locality)
    if (!free_list_.empty()) {
        Connection* conn = free_list_.back();
        free_list_.pop_back();

        // Check if connection is still valid and for same backend
        if (conn->is_valid() && conn->backend == backend) {
            conn->last_used = std::chrono::steady_clock::now();
            stats_.cache_hits++;
            stats_.active_connections++;
            return conn;
        }

        // Invalid or different backend, close it
        if (conn->is_valid()) {
            ::close(conn->sockfd);
        }
    }

    // Create new connection
    stats_.cache_misses++;

    auto conn = std::make_unique<Connection>();
    conn->backend = backend;
    conn->created_at = std::chrono::steady_clock::now();
    conn->last_used = conn->created_at;
    conn->sockfd = -1; // Placeholder - actual socket created by caller

    Connection* raw_ptr = conn.get();
    pool_.push_back(std::move(conn));

    stats_.total_connections++;
    stats_.active_connections++;

    return raw_ptr;
}

void ConnectionPool::release(Connection* conn) {
    if (!conn) return;

    conn->last_used = std::chrono::steady_clock::now();
    conn->requests_served++;

    // Check if connection should be closed
    if (!conn->keep_alive || conn->is_old(std::chrono::hours(1))) {
        close(conn);
        return;
    }

    // Return to free list
    free_list_.push_back(conn);
    stats_.idle_connections++;
    stats_.active_connections--;
}

void ConnectionPool::close(Connection* conn) {
    if (!conn) return;

    if (conn->is_valid()) {
        ::close(conn->sockfd);
        conn->sockfd = -1;
    }

    stats_.active_connections--;
}

void ConnectionPool::evict_expired(std::chrono::seconds max_idle) {
    auto it = std::remove_if(free_list_.begin(), free_list_.end(),
        [max_idle, this](Connection* conn) {
            if (conn->is_expired(max_idle)) {
                close(conn);
                return true;
            }
            return false;
        });

    size_t removed = std::distance(it, free_list_.end());
    free_list_.erase(it, free_list_.end());
    stats_.idle_connections -= removed;
}

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

// Upstream implementation

Upstream::Upstream(std::string name)
    : name_(std::move(name))
    , balancer_(std::make_unique<RoundRobinBalancer>())
    , pool_(2000) {}  // Increased from 100 to 2000 to handle extreme concurrency

Upstream::~Upstream() = default;

Upstream::Upstream(Upstream&&) noexcept = default;
Upstream& Upstream::operator=(Upstream&&) noexcept = default;

void Upstream::add_backend(Backend backend) {
    backends_.push_back(std::move(backend));
}

void Upstream::remove_backend(std::string_view address) {
    backends_.erase(
        std::remove_if(backends_.begin(), backends_.end(),
            [address](const Backend& b) { return b.address() == address; }),
        backends_.end());
}

Connection* Upstream::get_connection(std::string_view client_ip) {
    // Select backend
    Backend* backend = balancer_->select(backends_, client_ip);
    if (!backend) {
        return nullptr;
    }

    // Get connection from pool
    Connection* conn = pool_.acquire(backend);
    if (conn) {
        backend->active_connections++;
        backend->total_requests.fetch_add(1, std::memory_order_relaxed);
    }

    return conn;
}

void Upstream::release_connection(Connection* conn) {
    if (!conn || !conn->backend) {
        return;
    }

    conn->backend->active_connections--;
    pool_.release(conn);
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

    stats.pool_stats = pool_.get_stats();
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
