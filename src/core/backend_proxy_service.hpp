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

// Backend Proxy Service - Header
// Manages backend connections and async I/O operations

#pragma once

#include <netinet/in.h>
#include <quill/Logger.h>

#include <string>

#include "containers.hpp"

namespace titan::core {

// Forward declarations
struct Connection;
struct BackendConnection;

/// Backend Proxy Service - Manages backend connections and async I/O
///
/// Responsibilities:
/// - DNS resolution and caching
/// - Backend connection establishment (sync + async)
/// - Backend event handling (dual epoll pattern)
/// - Backend operation processing (connect, send, recv)
///
/// Data-Oriented Design:
/// - Separate epoll instance for backend sockets
/// - DNS cache for hostname resolution
/// - Backend connection tracking map
class BackendProxyService {
public:
    /// Create backend proxy service
    BackendProxyService();
    ~BackendProxyService();

    BackendProxyService(const BackendProxyService&) = delete;
    BackendProxyService& operator=(const BackendProxyService&) = delete;

    /// Set logger for this service (called after construction)
    void set_logger(quill::Logger* logger) noexcept { logger_ = logger; }

    /// Get backend epoll fd for worker to monitor
    [[nodiscard]] int backend_epoll_fd() const noexcept { return backend_epoll_fd_; }

    /// Connect to backend server (blocking - for legacy code paths)
    [[nodiscard]] int connect_to_backend(const std::string& host, uint16_t port);

    /// Connect to backend server (non-blocking for async proxy)
    /// Returns socket fd in connecting state, or -1 on immediate failure
    [[nodiscard]] int connect_to_backend_async(const std::string& host, uint16_t port);

    /// Add backend socket to backend epoll for monitoring
    [[nodiscard]] bool add_backend_to_epoll(int backend_fd, uint32_t events);

    /// Remove backend socket from backend epoll
    [[nodiscard]] bool remove_backend_from_epoll(int backend_fd);

    /// Register backend connection mapping (backend_fd -> client_fd/stream_id)
    void register_backend_connection(int backend_fd, int client_fd, int32_t stream_id = -1);

    /// Unregister backend connection mapping
    void unregister_backend_connection(int backend_fd);

    /// Get client connection info for backend fd
    /// Returns (client_fd, stream_id) or (-1, -1) if not found
    [[nodiscard]] std::pair<int, int32_t> get_client_for_backend(int backend_fd) const;

private:
    quill::Logger* logger_ = nullptr;

    // DNS resolution cache (hostname -> resolved address)
    // Cache is never invalidated for simplicity (MVP)
    // TODO: Add TTL-based expiration for production
    fast_map<std::string, sockaddr_in> dns_cache_;

    // Dual epoll for non-blocking backend I/O
    int backend_epoll_fd_ = -1;  // Separate epoll instance for backend sockets

    // Map backend_fd -> (client_fd, stream_id) to avoid storing dangling raw pointers
    // stream_id = -1 for HTTP/1.1, >= 0 for HTTP/2 streams
    fast_map<int, std::pair<int, int32_t>> backend_connections_;
};

}  // namespace titan::core
