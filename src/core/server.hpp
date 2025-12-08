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

// Titan Server - Header
// HTTP server managing connections and request processing

#pragma once

#include <netinet/in.h>
#include <openssl/ssl.h>
#include <quill/Logger.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../control/config.hpp"
#include "../control/prometheus.hpp"
#include "../gateway/pipeline.hpp"
#include "../gateway/router.hpp"
#include "../gateway/upstream.hpp"
#include "../http/h2.hpp"
#include "../http/parser.hpp"
#include "core.hpp"
#include "socket.hpp"
#include "tls.hpp"

// Forward declaration for test access
class ProxyTestFixture;

namespace titan::core {

/// Connection protocol type
enum class Protocol : uint8_t {
    Unknown,   // Not yet determined
    HTTP_1_1,  // HTTP/1.1
    HTTP_2,    // HTTP/2
};

/// Backend connection state for async proxy operations
struct BackendConnection {
    int backend_fd = -1;               // Backend socket file descriptor
    int client_fd = -1;                // Associated client connection
    int32_t stream_id = -1;            // HTTP/2 stream ID (for HTTP/2 connections)
    std::string upstream_name;         // Upstream name (for connection pooling)
    std::string backend_host;          // Backend host (for connection pooling)
    uint16_t backend_port = 0;         // Backend port (for connection pooling)
    bool connect_pending = false;      // Waiting for non-blocking connect to complete
    bool send_pending = false;         // Waiting to send request
    bool recv_pending = false;         // Waiting to receive response
    bool cleanup_pending = false;      // Response sent, can be cleaned up
    std::vector<uint8_t> send_buffer;  // Request data to send
    std::vector<uint8_t> recv_buffer;  // Response data received
    size_t send_cursor = 0;            // How much of send_buffer has been sent

    // Timing for response middleware
    std::chrono::steady_clock::time_point start_time;
    std::unordered_map<std::string, std::string> metadata;  // For middleware communication
};

/// Active client connection
struct Connection {
    int fd = -1;
    std::string remote_ip;
    uint16_t remote_port = 0;

    Protocol protocol = Protocol::Unknown;
    std::vector<uint8_t> recv_buffer;
    size_t recv_cursor = 0;  // Current read position in recv_buffer (avoids expensive erase)
    std::vector<uint8_t> response_body;  // Owned body data for proxied responses
    std::vector<std::pair<std::string, std::string>>
        response_header_storage;  // Owned header strings

    // TLS state
    SSL* ssl = nullptr;        // OpenSSL connection object (owned by unique_ptr in Server)
    bool tls_enabled = false;  // Whether this connection uses TLS
    bool tls_handshake_complete = false;  // TLS handshake completion state

    // HTTP/1.1 state
    http::Parser parser;
    http::Request request;
    http::Response response;
    bool keep_alive = true;

    // HTTP/2 state
    std::unique_ptr<http::H2Session> h2_session;
    // Map stream_id → backend connection (for HTTP/2 multiplexing)
    std::unordered_map<int32_t, std::unique_ptr<BackendConnection>> h2_stream_backends;

    // Backend proxy state (for async operations - HTTP/1.1 only)
    std::unique_ptr<BackendConnection> backend_conn;
};

/// HTTP server managing connections
class Server {
    // Allow test fixture to access private methods
    friend class ::ProxyTestFixture;

public:
    /// Create server with configuration and pre-built components
    explicit Server(const control::Config& config, std::unique_ptr<gateway::Router> router,
                    std::unique_ptr<gateway::UpstreamManager> upstream_manager,
                    std::unique_ptr<gateway::Pipeline> pipeline);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Start server (bind and listen)
    [[nodiscard]] std::error_code start();

    /// Stop server
    void stop();

    [[nodiscard]] int listen_fd() const noexcept { return listen_fd_; }

    /// Get upstream manager (for metrics/admin server)
    [[nodiscard]] const gateway::UpstreamManager* upstream_manager() const noexcept {
        return upstream_manager_.get();
    }

    /// Set logger for this worker
    void set_logger(quill::Logger* logger) noexcept { logger_ = logger; }

    /// Process incoming connection
    void handle_accept(int client_fd, std::string_view remote_ip, uint16_t remote_port);

    /// Process data from connection (reads from socket internally)
    void handle_read(int client_fd);

    /// Handle connection close
    void handle_close(int client_fd);

    /// Backend event handling (dual epoll pattern)
    /// Returns backend epoll fd for worker to monitor
    [[nodiscard]] int backend_epoll_fd() const noexcept { return backend_epoll_fd_; }

    /// Process backend connection event (called by worker when backend epoll fires)
    void handle_backend_event(int backend_fd, bool readable, bool writable, bool error);

    /// Process pending backend operations (connect, send, recv)
    void process_backend_operations();

    /// Add backend socket to backend epoll for monitoring
    [[nodiscard]] bool add_backend_to_epoll(int backend_fd, uint32_t events);

    /// Remove backend socket from backend epoll
    [[nodiscard]] bool remove_backend_from_epoll(int backend_fd);

private:
    const control::Config& config_;
    int listen_fd_ = -1;
    bool running_ = false;

    std::unique_ptr<gateway::Router> router_;
    std::unique_ptr<gateway::UpstreamManager> upstream_manager_;
    std::unique_ptr<gateway::Pipeline> pipeline_;

    quill::Logger* logger_ = nullptr;

    // TLS support
    std::optional<TlsContext> tls_context_;
    std::unordered_map<int, SslPtr> ssl_connections_;  // fd -> SSL object mapping

    std::unordered_map<int, std::unique_ptr<Connection>> connections_;

    // DNS resolution cache (hostname -> resolved address)
    // Cache is never invalidated for simplicity (MVP)
    // TODO: Add TTL-based expiration for production
    std::unordered_map<std::string, sockaddr_in> dns_cache_;

    // Dual epoll for non-blocking backend I/O
    int backend_epoll_fd_ = -1;  // Separate epoll instance for backend sockets
    // Map backend_fd -> (client_fd, stream_id) to avoid storing dangling raw pointers
    // stream_id = -1 for HTTP/1.1, >= 0 for HTTP/2 streams
    std::unordered_map<int, std::pair<int, int32_t>> backend_connections_;

    /// Process request and send response
    /// returns false if connection was/should be closed
    bool process_request(Connection& conn);
    void send_response(Connection& conn, bool keep_alive);

    /// Protocol-specific handlers
    void handle_http1(Connection& conn);
    void handle_http2(Connection& conn);
    void process_http2_stream(Connection& conn, http::H2Stream& stream);

    /// Proxy request to backend using context (for middleware integration)
    bool proxy_to_backend(Connection& conn, gateway::RequestContext& ctx);

    /// Connect to backend server (blocking - legacy)
    [[nodiscard]] int connect_to_backend(const std::string& host, uint16_t port);

    /// Connect to backend server (non-blocking for async proxy)
    /// Returns socket fd in connecting state, or -1 on immediate failure
    [[nodiscard]] int connect_to_backend_async(const std::string& host, uint16_t port);

    /// Build HTTP request string to send to backend
    std::string build_backend_request(const http::Request& request,
                                      const std::unordered_map<std::string, std::string>& metadata);

    /// Receive and parse HTTP response from backend
    bool receive_backend_response(int backend_fd, http::Response& response,
                                  std::vector<uint8_t>& body);
};

}  // namespace titan::core
