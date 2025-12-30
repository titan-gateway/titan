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

// Titan Gateway - Backend Connection Pool Implementation

#include "connection_pool.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include "../core/logging.hpp"
#include "../core/socket.hpp"

using titan::core::close_fd;

namespace titan::gateway {

bool PooledConnection::is_healthy() const noexcept {
    if (fd < 0)
        return false;

    // Health check using MSG_PEEK to detect if remote end has closed (CLOSE-WAIT)
    // recv() with MSG_PEEK|MSG_DONTWAIT returns:
    // - >0: data available (connection is alive and has data)
    // - 0: remote end closed (FIN received) → connection is dead
    // - <0 with EAGAIN/EWOULDBLOCK: no data available but connection is healthy
    // - <0 with other errors: connection is broken
    char buf[1];
    ssize_t result = recv(fd, buf, 1, MSG_PEEK | MSG_DONTWAIT);

    if (result > 0) {
        // Data available - connection is healthy (backend sent unsolicited data, rare but valid)
        return true;
    } else if (result == 0) {
        // Remote end closed - connection is dead (CLOSE-WAIT state)
        return false;
    } else {
        // Check errno
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available but connection is healthy
            return true;
        }
        // Other error - connection is broken
        return false;
    }
}

BackendConnectionPool::BackendConnectionPool(size_t max_size, std::chrono::seconds max_idle,
                                             size_t max_requests_per_conn)
    : max_size_(max_size), max_idle_(max_idle), max_requests_per_conn_(max_requests_per_conn) {
    pool_.reserve(max_size);
}

BackendConnectionPool::~BackendConnectionPool() {
    clear();
}

int BackendConnectionPool::acquire(const std::string& host, uint16_t port, bool require_http2) {
    // Search pool from back to front (LIFO - most recently used first)
    for (auto it = pool_.rbegin(); it != pool_.rend(); ++it) {
        // CRITICAL: Match host, port, AND protocol (HTTP/2 vs HTTP/1.1)
        // This prevents returning HTTP/1.1 connections for gRPC requests
        if (it->host == host && it->port == port && it->is_http2 == require_http2) {
            int fd = it->fd;

            // Check if connection needs recycling (served too many requests)
            size_t request_count = fd_request_counts_[fd];
            if (max_requests_per_conn_ > 0 && request_count >= max_requests_per_conn_) {
                // Recycle - close and remove from pool
                close_fd(fd);
                fd_request_counts_.erase(fd);
                pool_.erase(std::next(it).base());
                ++evictions_;
                // Continue searching for another connection
                continue;
            }

            // Found matching connection - check if healthy
            if (it->is_healthy()) {
                // Remove from pool (convert reverse iterator to forward iterator)
                pool_.erase(std::next(it).base());

                ++hits_;
                return fd;
            } else {
                // Unhealthy - close and remove
                close_fd(fd);
                fd_request_counts_.erase(fd);
                pool_.erase(std::next(it).base());
                ++health_fails_;
                // Continue searching
            }
        }
    }

    // No matching or healthy connection found
    ++misses_;
    return -1;
}

void BackendConnectionPool::release(int fd, const std::string& host, uint16_t port, bool is_http2) {
    if (fd < 0)
        return;

    // Increment request count for this FD
    fd_request_counts_[fd]++;

    // Check if connection needs recycling after this request
    if (max_requests_per_conn_ > 0 && fd_request_counts_[fd] >= max_requests_per_conn_) {
        // Recycle - close connection instead of pooling
        close_fd(fd);
        fd_request_counts_.erase(fd);
        ++evictions_;
        return;
    }

    // Check if pool is full
    if (pool_.size() >= max_size_) {
        // Pool full - close connection
        close_fd(fd);
        fd_request_counts_.erase(fd);
        ++pool_full_closes_;
        return;
    }

    // Quick health check before adding to pool
    PooledConnection conn;
    conn.fd = fd;
    conn.host = host;
    conn.port = port;
    conn.is_http2 = is_http2;  // CRITICAL: Store protocol for correct matching
    conn.last_used = std::chrono::steady_clock::now();
    conn.request_count = fd_request_counts_[fd];  // Store current count for debugging

    if (!conn.is_healthy()) {
        // Unhealthy - close instead of pooling
        close_fd(fd);
        fd_request_counts_.erase(fd);
        ++health_fails_;
        return;
    }

    // Add to pool (LIFO - push to back)
    pool_.push_back(std::move(conn));
}

void BackendConnectionPool::cleanup_stale() {
    auto now = std::chrono::steady_clock::now();

    // Remove stale connections
    pool_.erase(std::remove_if(pool_.begin(), pool_.end(),
                               [this, now](const PooledConnection& conn) {
                                   if (conn.is_stale(max_idle_)) {
                                       close_fd(conn.fd);
                                       fd_request_counts_.erase(conn.fd);  // Clean up request count
                                       return true;  // Remove from pool
                                   }
                                   return false;
                               }),
                pool_.end());
}

void BackendConnectionPool::clear() {
    // Close all connections
    for (const auto& conn : pool_) {
        if (conn.fd >= 0) {
            close_fd(conn.fd);
        }
    }
    pool_.clear();
    fd_request_counts_.clear();  // Clear request count tracking
}

void BackendConnectionPool::log_stats() const {
    auto* logger = logging::get_current_logger();
    if (!logger) {
        return;
    }

    auto total_requests = hits_ + misses_;
    if (total_requests == 0) {
        LOG_INFO(logger, "[POOL] No requests processed yet");
        return;
    }

    LOG_INFO(logger,
             "[POOL] Stats: size={}/{}, hits={}, misses={}, hit_rate={:.2f}%, "
             "health_fails={}, pool_full_closes={}, evictions={}, max_requests_per_conn={}",
             pool_.size(), max_size_, hits_, misses_, hit_rate() * 100.0, health_fails_,
             pool_full_closes_, evictions_, max_requests_per_conn_);
}

}  // namespace titan::gateway
