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

// Titan Gateway - Backend Connection Pool
// Per-worker, per-upstream connection pool for reusing backend connections

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "../core/containers.hpp"  // fast_map

namespace titan::gateway {

/// Pooled backend connection with metadata
struct PooledConnection {
    int fd = -1;
    std::string host;
    uint16_t port = 0;
    bool is_http2 = false;  // Protocol: true = HTTP/2, false = HTTP/1.1
    std::chrono::steady_clock::time_point last_used;
    size_t request_count = 0;  // Number of requests served by this connection

    /// Check if connection has been idle too long
    [[nodiscard]] bool is_stale(std::chrono::seconds max_idle) const noexcept {
        auto now = std::chrono::steady_clock::now();
        return (now - last_used) > max_idle;
    }

    /// Check if connection has served too many requests (needs recycling)
    [[nodiscard]] bool needs_recycling(size_t max_requests) const noexcept {
        return max_requests > 0 && request_count >= max_requests;
    }

    /// Quick health check: try to send 0 bytes (returns true if socket is writable)
    [[nodiscard]] bool is_healthy() const noexcept;
};

/// Backend connection pool (LIFO stack, no locking - thread-local)
///
/// Each worker thread maintains its own pool per upstream.
/// LIFO (stack) strategy provides better cache locality than FIFO.
class BackendConnectionPool {
public:
    /// Create pool with max size, idle timeout, and max requests per connection
    /// @param max_size Maximum number of pooled connections
    /// @param max_idle Maximum idle time before connection is evicted
    /// @param max_requests_per_conn Maximum requests per connection before recycling (0 = unlimited)
    explicit BackendConnectionPool(size_t max_size = 64,
                                   std::chrono::seconds max_idle = std::chrono::seconds(30),
                                   size_t max_requests_per_conn = 0);

    BackendConnectionPool(const BackendConnectionPool&) = delete;
    BackendConnectionPool& operator=(const BackendConnectionPool&) = delete;

    BackendConnectionPool(BackendConnectionPool&&) noexcept = default;
    BackendConnectionPool& operator=(BackendConnectionPool&&) noexcept = default;

    ~BackendConnectionPool();

    /// Try to acquire a connection from the pool
    /// Returns -1 if pool is empty or no matching connection found
    /// Performs health check before returning connection
    /// require_http2: if true, only return HTTP/2 connections; if false, only return HTTP/1.1
    [[nodiscard]] int acquire(const std::string& host, uint16_t port, bool require_http2);

    /// Return a connection to the pool
    /// If pool is full or connection is unhealthy, closes the connection
    /// is_http2: true if this connection uses HTTP/2, false for HTTP/1.1
    void release(int fd, const std::string& host, uint16_t port, bool is_http2);

    /// Remove stale connections (idle > max_idle_time)
    /// Should be called periodically (e.g., every 10 seconds)
    void cleanup_stale();

    /// Clear all connections in pool
    void clear();

    // Statistics
    [[nodiscard]] size_t size() const noexcept { return pool_.size(); }
    [[nodiscard]] size_t max_size() const noexcept { return max_size_; }
    [[nodiscard]] size_t max_requests_per_conn() const noexcept { return max_requests_per_conn_; }
    [[nodiscard]] size_t hits() const noexcept { return hits_; }
    [[nodiscard]] size_t misses() const noexcept { return misses_; }
    [[nodiscard]] size_t health_fails() const noexcept { return health_fails_; }
    [[nodiscard]] size_t pool_full_closes() const noexcept { return pool_full_closes_; }
    [[nodiscard]] size_t evictions() const noexcept { return evictions_; }
    [[nodiscard]] double hit_rate() const noexcept {
        auto total = hits_ + misses_;
        return total > 0 ? static_cast<double>(hits_) / total : 0.0;
    }

    /// Log pool statistics (call periodically for monitoring)
    void log_stats() const;

private:
    std::vector<PooledConnection> pool_;  // LIFO stack (back = top)
    size_t max_size_;
    std::chrono::seconds max_idle_;
    size_t max_requests_per_conn_;  // Max requests before recycling (0 = unlimited)

    // Track request count for each FD (persists across acquire/release cycles)
    // Using fast_map (ankerl::unordered_dense) for 25% faster lookups
    titan::core::fast_map<int, size_t> fd_request_counts_;

    // Statistics
    size_t hits_ = 0;              // Pool hit (reused connection)
    size_t misses_ = 0;            // Pool miss (created new connection)
    size_t health_fails_ = 0;      // Health check failures
    size_t pool_full_closes_ = 0;  // Closes due to pool being full
    size_t evictions_ = 0;         // Connections recycled after max_requests_per_conn
};

}  // namespace titan::gateway
