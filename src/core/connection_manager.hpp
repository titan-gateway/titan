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

// Connection Manager - Header
// Manages client connection lifecycle and TLS operations

#pragma once

#include <openssl/ssl.h>
#include <quill/Logger.h>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "containers.hpp"
#include "tls.hpp"

// Forward declarations
namespace titan::http {
struct H2Session;
struct WebSocketConnection;
}  // namespace titan::http

namespace titan::core {

// Forward declare Protocol enum (defined in server.hpp for now)
// TODO: Move to separate types.hpp in future refactoring
enum class Protocol : uint8_t;

// Forward declare Connection struct (defined in server.hpp for now)
// TODO: Move to separate types.hpp in future refactoring
struct Connection;
struct BackendConnection;

/// Result of TLS handshake operation with protocol detection
struct TlsHandshakeComplete {
    Protocol negotiated_protocol;  // HTTP/1.1, HTTP/2, or unknown
};

/// Connection Manager - Handles client connection lifecycle and TLS
///
/// Responsibilities:
/// - Accept new client connections
/// - TLS handshake and ALPN protocol negotiation
/// - TLS read/write operations
/// - Connection cleanup and resource management
/// - Connection storage and lookup
///
/// Data-Oriented Design:
/// - All connections stored in contiguous map for cache locality
/// - TLS objects stored separately (hot/cold data split)
/// - No virtual functions (zero overhead)
class ConnectionManager {
public:
    /// Create connection manager with optional TLS support
    /// @param tls_context Optional TLS context for secure connections
    /// @param logger Logger instance for diagnostics
    explicit ConnectionManager(std::optional<TlsContext> tls_context, quill::Logger* logger);

    ~ConnectionManager();

    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&) = default;
    ConnectionManager& operator=(ConnectionManager&&) = default;

    /// Accept new client connection
    /// Creates Connection object and initializes TLS if enabled
    /// @param client_fd Socket file descriptor
    /// @param remote_ip Client IP address
    /// @param remote_port Client port number
    /// @return Pointer to new Connection or nullptr on failure
    [[nodiscard]] Connection* accept(int client_fd, std::string_view remote_ip,
                                     uint16_t remote_port);

    /// Get connection by file descriptor
    /// @param client_fd Socket file descriptor
    /// @return Pointer to Connection or nullptr if not found
    [[nodiscard]] Connection* get(int client_fd) noexcept;

    /// Get connection by file descriptor (const version)
    /// @param client_fd Socket file descriptor
    /// @return Pointer to Connection or nullptr if not found
    [[nodiscard]] const Connection* get(int client_fd) const noexcept;

    /// Perform TLS handshake on connection (non-blocking)
    /// Detects ALPN protocol and initializes HTTP/2 session if needed
    /// @param conn Connection to perform handshake on
    /// @return TlsHandshakeComplete with protocol if successful, nullopt if needs retry/error
    [[nodiscard]] std::optional<TlsHandshakeComplete> handle_tls_handshake(Connection& conn);

    /// Read data from connection (handles TLS if enabled)
    /// @param conn Connection to read from
    /// @param buffer Buffer to read into
    /// @return Bytes read, 0 on EOF, -1 on error (check errno for EAGAIN/EWOULDBLOCK)
    [[nodiscard]] int read(Connection& conn, std::span<uint8_t> buffer) noexcept;

    /// Write data to connection (handles TLS if enabled)
    /// @param conn Connection to write to
    /// @param data Data to write
    /// @return Bytes written, -1 on error (check errno for EAGAIN/EWOULDBLOCK)
    [[nodiscard]] int write(Connection& conn, std::span<const uint8_t> data) noexcept;

    /// Close connection and cleanup resources
    /// Removes from storage, closes SSL, handles backend cleanup
    /// @param client_fd Socket file descriptor
    /// @param backend_epoll_fd Backend epoll FD for cleanup (pass -1 if not using dual epoll)
    void close(int client_fd, int backend_epoll_fd = -1);

    /// Get number of active connections
    [[nodiscard]] size_t connection_count() const noexcept { return connections_.size(); }

    /// Check if TLS is enabled
    [[nodiscard]] bool is_tls_enabled() const noexcept { return tls_context_.has_value(); }

    /// Set logger for this connection manager (called after construction)
    void set_logger(quill::Logger* logger) noexcept { logger_ = logger; }

private:
    std::optional<TlsContext> tls_context_;
    quill::Logger* logger_;

    // Connection storage (Data-Oriented: separate hot/cold data)
    fast_map<int, std::unique_ptr<Connection>> connections_;  // fd -> Connection
    fast_map<int, SslPtr> ssl_connections_;                   // fd -> SSL object (if TLS enabled)

    /// Helper: Close backend connection (HTTP/1.1, HTTP/2 streams, WebSocket)
    void close_backend_connection(BackendConnection* backend_conn, int backend_epoll_fd);
};

}  // namespace titan::core
