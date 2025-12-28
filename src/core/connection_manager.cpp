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

// Connection Manager - Implementation
// Manages client connection lifecycle and TLS operations

#include "connection_manager.hpp"

#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#endif

#include "../http/h2.hpp"
#include "../http/websocket.hpp"
#include "logging.hpp"
#include "server.hpp"  // For Connection, BackendConnection, Protocol definitions
#include "socket.hpp"

namespace titan::core {

ConnectionManager::ConnectionManager(std::optional<TlsContext> tls_context, quill::Logger* logger)
    : tls_context_(std::move(tls_context)), logger_(logger) {}

ConnectionManager::~ConnectionManager() = default;

Connection* ConnectionManager::accept(int client_fd, std::string_view remote_ip,
                                      uint16_t remote_port) {
    auto conn = std::make_unique<Connection>();
    conn->fd = client_fd;
    conn->remote_ip = remote_ip;
    conn->remote_port = remote_port;
    conn->recv_buffer.reserve(8192);

    // Create SSL object if TLS is enabled
    if (tls_context_) {
        auto ssl = tls_context_->create_ssl(client_fd);
        if (ssl) {
            conn->ssl = ssl.get();
            conn->tls_enabled = true;
            ssl_connections_[client_fd] = std::move(ssl);
        } else {
            // Failed to create SSL object - close connection
            LOG_ERROR(logger_, "Failed to create SSL object for fd={}", client_fd);
            close_fd(client_fd);
            return nullptr;
        }
    }

    Connection* conn_ptr = conn.get();
    connections_[client_fd] = std::move(conn);

    return conn_ptr;
}

Connection* ConnectionManager::get(int client_fd) noexcept {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const Connection* ConnectionManager::get(int client_fd) const noexcept {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::optional<TlsHandshakeComplete> ConnectionManager::handle_tls_handshake(Connection& conn) {
    // Only perform handshake if TLS is enabled and not yet complete
    if (!conn.tls_enabled || conn.tls_handshake_complete) {
        return std::nullopt;
    }

    auto result = ssl_accept_nonblocking(conn.ssl);

    if (result == TlsHandshakeResult::Complete) {
        conn.tls_handshake_complete = true;

        // Get negotiated protocol from ALPN
        auto alpn_protocol = get_alpn_protocol(conn.ssl);

        TlsHandshakeComplete completion;
        if (alpn_protocol == "h2") {
            conn.protocol = Protocol::HTTP_2;
            conn.h2_session = std::make_unique<http::H2Session>(true);  // server mode
            completion.negotiated_protocol = Protocol::HTTP_2;
        } else {
            // Default to HTTP/1.1 (even if ALPN selected "http/1.1" or no ALPN)
            conn.protocol = Protocol::HTTP_1_1;
            completion.negotiated_protocol = Protocol::HTTP_1_1;
        }

        LOG_DEBUG(logger_, "TLS handshake complete for fd={}, protocol={}", conn.fd,
                  alpn_protocol.empty() ? "http/1.1 (default)" : alpn_protocol);

        return completion;
    } else if (result == TlsHandshakeResult::WantRead || result == TlsHandshakeResult::WantWrite) {
        // Handshake in progress - caller should retry later
        return std::nullopt;
    } else {
        // Handshake error
        LOG_WARNING(logger_, "TLS handshake error for fd={}", conn.fd);
        return std::nullopt;  // Caller should close connection
    }
}

int ConnectionManager::read(Connection& conn, std::span<uint8_t> buffer) noexcept {
    if (conn.tls_enabled) {
        return ssl_read_nonblocking(conn.ssl, buffer);
    } else {
        return ::read(conn.fd, buffer.data(), buffer.size());
    }
}

int ConnectionManager::write(Connection& conn, std::span<const uint8_t> data) noexcept {
    if (conn.tls_enabled) {
        return ssl_write_nonblocking(conn.ssl, data);
    } else {
        return ::write(conn.fd, data.data(), data.size());
    }
}

void ConnectionManager::close_backend_connection(BackendConnection* backend_conn,
                                                  int backend_epoll_fd) {
    if (!backend_conn) {
        return;
    }

    int backend_fd = backend_conn->backend_fd;
    if (backend_fd < 0) {
        return;
    }

    // Remove from backend epoll (if valid fd provided)
    if (backend_epoll_fd >= 0) {
#ifdef __linux__
        epoll_ctl(backend_epoll_fd, EPOLL_CTL_DEL, backend_fd, nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__)
        struct kevent kev;
        EV_SET(&kev, backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(backend_epoll_fd, &kev, 1, nullptr, 0, nullptr);
        EV_SET(&kev, backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(backend_epoll_fd, &kev, 1, nullptr, 0, nullptr);
#endif
    }

    // Close backend socket
    close_fd(backend_fd);
}

void ConnectionManager::close(int client_fd, int backend_epoll_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return;
    }

    Connection& conn = *it->second;

    // Clean up WebSocket backend connection if exists
    if (conn.ws_conn && conn.ws_conn->backend_fd >= 0) {
        int backend_fd = conn.ws_conn->backend_fd;

        // Remove from backend epoll and close socket
        if (backend_epoll_fd >= 0) {
#ifdef __linux__
            epoll_ctl(backend_epoll_fd, EPOLL_CTL_DEL, backend_fd, nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__)
            struct kevent kev;
            EV_SET(&kev, backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(backend_epoll_fd, &kev, 1, nullptr, 0, nullptr);
            EV_SET(&kev, backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(backend_epoll_fd, &kev, 1, nullptr, 0, nullptr);
#endif
        }

        close_fd(backend_fd);
        conn.ws_conn->backend_fd = -1;
        conn.ws_conn->state = http::WebSocketState::CLOSED;

        LOG_DEBUG(logger_, "Cleaned up WebSocket backend: backend_fd={}", backend_fd);
    }

    // Clean up backend connection if exists (HTTP/1.1)
    if (conn.backend_conn) {
        close_backend_connection(conn.backend_conn.get(), backend_epoll_fd);
    }

    // Clean up all HTTP/2 stream backend connections
    for (auto& [stream_id, stream_backend] : conn.h2_stream_backends) {
        close_backend_connection(stream_backend.get(), backend_epoll_fd);
    }

    // Clean up SSL connection if exists
    ssl_connections_.erase(client_fd);

    // Close client socket and remove connection
    close_fd(conn.fd);
    connections_.erase(it);

    LOG_DEBUG(logger_, "Closed connection fd={}", client_fd);
}

}  // namespace titan::core
