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

// Titan Admin Server - Header
// Lightweight HTTP server for internal admin endpoints (metrics, health)
// Runs on separate port (default 9090), NOT exposed to public internet

#pragma once

#include <atomic>
#include <memory>
#include <system_error>

#include "../control/config.hpp"
#include "../gateway/upstream.hpp"
#include "jwt_revocation.hpp"

namespace titan::core {

/// Lightweight admin server for internal endpoints
/// Serves /health and /metrics on a separate port (internal only)
/// Uses simple blocking I/O (not performance-critical)
class AdminServer {
public:
    /// Create admin server with config and upstream manager
    explicit AdminServer(const control::Config& config,
                         const gateway::UpstreamManager* upstream_manager,
                         RevocationQueue* revocation_queue = nullptr);
    ~AdminServer();

    // Non-copyable, non-movable
    AdminServer(const AdminServer&) = delete;
    AdminServer& operator=(const AdminServer&) = delete;

    /// Start admin server (bind and listen on metrics port)
    [[nodiscard]] std::error_code start();

    /// Stop admin server
    void stop();

    /// Run server event loop (blocking, call in separate thread)
    void run();

    /// Check if server is running
    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_relaxed);
    }

private:
    const control::Config& config_;
    const gateway::UpstreamManager* upstream_manager_;
    RevocationQueue* revocation_queue_;  // Global revocation queue (nullable)

    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    uint32_t worker_id_ = 0;  // Which worker is this admin server for (0 for global)

    /// Handle single client connection (blocking)
    void handle_connection(int client_fd);

    /// Parse simple HTTP request (minimal parser for GET only)
    struct SimpleRequest {
        std::string method;
        std::string path;
        bool valid = false;
    };
    [[nodiscard]] SimpleRequest parse_request(const char* data, size_t len);

    /// Send HTTP response
    void send_response(int fd, int status_code, std::string_view content_type,
                       std::string_view body);
};

}  // namespace titan::core
