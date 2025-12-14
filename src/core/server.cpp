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

// Titan Server - Implementation

#include "server.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <unordered_set>

#include "logging.hpp"
#include "socket.hpp"

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#endif

namespace titan::core {

// Backend proxy performance tuning constants
namespace {
// Buffer sizes optimized for typical API responses
constexpr size_t kBackendResponseBufferSize = 65536;           // 64KB - most API responses fit
constexpr size_t kBackendReadChunkSize = 8192;                 // 8KB - fewer syscalls
constexpr size_t kBackendMaxResponseSize = 100 * 1024 * 1024;  // 100MB safety limit

// Request building size estimates
constexpr size_t kRequestLineBaseSize = 50;  // "METHOD /path HTTP/1.1\r\n"
constexpr size_t kRequestHeaderMargin = 50;  // Extra for Connection, Host headers
constexpr size_t kHeaderSeparatorSize = 4;   // ": \r\n"
constexpr size_t kQuerySeparatorSize = 1;    // "?"

// Connection staleness check threshold
// Only perform expensive MSG_PEEK validation if connection idle > 5s
constexpr auto kConnectionStaleThreshold = std::chrono::seconds(5);
}  // anonymous namespace

Server::Server(const control::Config& config, std::unique_ptr<gateway::Router> router,
               std::unique_ptr<gateway::UpstreamManager> upstream_manager,
               std::unique_ptr<gateway::Pipeline> pipeline)
    : config_(config),
      router_(std::move(router)),
      upstream_manager_(std::move(upstream_manager)),
      pipeline_(std::move(pipeline)) {
    // Initialize TLS if enabled
    if (config_.server.tls_enabled) {
        std::error_code error;
        auto result = TlsContext::create(config_.server.tls_certificate_path,
                                         config_.server.tls_private_key_path,
                                         config_.server.tls_alpn_protocols, error);

        if (result) {
            tls_context_ = std::move(*result);
        } else {
            throw std::runtime_error("Failed to initialize TLS context: " + error.message());
        }
    }

    // Create backend epoll/kqueue instance for non-blocking backend I/O
#ifdef __linux__
    backend_epoll_fd_ = epoll_create1(0);
    if (backend_epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create backend epoll instance");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    backend_epoll_fd_ = kqueue();
    if (backend_epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create backend kqueue instance");
    }
#endif
}

Server::~Server() {
    stop();

    // Close backend epoll/kqueue instance
    if (backend_epoll_fd_ >= 0) {
        close_fd(backend_epoll_fd_);
        backend_epoll_fd_ = -1;
    }
}

std::error_code Server::start() {
    if (running_) {
        return {};
    }

    // Increase file descriptor limit to handle extreme concurrency
    // Default limit is often too low (1024) for high-connection scenarios
    struct rlimit fd_limit;
    fd_limit.rlim_cur = 65536;  // Soft limit
    fd_limit.rlim_max = 65536;  // Hard limit
    if (setrlimit(RLIMIT_NOFILE, &fd_limit) < 0) {
        // Log warning but continue - not critical
        // In production, this should be set via systemd LimitNOFILE or ulimit
    }

    listen_fd_ = create_listening_socket(config_.server.listen_address, config_.server.listen_port,
                                         config_.server.backlog);

    if (listen_fd_ < 0) {
        return std::error_code(errno, std::system_category());
    }

    running_ = true;
    return {};
}

void Server::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // Close all client connections
    for (auto& [fd, conn] : connections_) {
        close_fd(conn->fd);
    }
    connections_.clear();

    // Close listening socket
    if (listen_fd_ >= 0) {
        close_fd(listen_fd_);
        listen_fd_ = -1;
    }
}

void Server::handle_accept(int client_fd, std::string_view remote_ip, uint16_t remote_port) {
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
            close_fd(client_fd);
            return;
        }
    }

    connections_[client_fd] = std::move(conn);
}

void Server::handle_read(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return;
    }

    Connection& conn = *it->second;

    // TLS handshake if enabled and not yet complete
    if (conn.tls_enabled && !conn.tls_handshake_complete) {
        auto result = ssl_accept_nonblocking(conn.ssl);

        if (result == TlsHandshakeResult::Complete) {
            conn.tls_handshake_complete = true;

            // Get negotiated protocol from ALPN
            auto alpn_protocol = get_alpn_protocol(conn.ssl);

            if (alpn_protocol == "h2") {
                conn.protocol = Protocol::HTTP_2;
                conn.h2_session = std::make_unique<http::H2Session>(true);  // server mode
            } else {
                // Default to HTTP/1.1 (even if ALPN selected "http/1.1" or no ALPN)
                conn.protocol = Protocol::HTTP_1_1;
            }
        } else if (result == TlsHandshakeResult::WantRead ||
                   result == TlsHandshakeResult::WantWrite) {
            // Handshake in progress - wait for more data
            return;
        } else {
            // Handshake error - close connection
            handle_close(client_fd);
            return;
        }
    }

    // Read data from socket (either SSL or raw)
    uint8_t buffer[8192];
    ssize_t n;

    if (conn.tls_enabled) {
        // TLS read - drain ALL data until WANT_READ (critical for HTTP/2 multiplexing + edge-triggered epoll)
        // With edge-triggered epoll, we MUST read until WANT_READ or we won't get notified again
        while (true) {
            n = ssl_read_nonblocking(conn.ssl, buffer);

            if (n > 0) {
                // Append to buffer
                conn.recv_buffer.insert(conn.recv_buffer.end(), buffer, buffer + n);
                // Continue reading - there might be more data
            } else {
                int err = SSL_get_error(conn.ssl, n);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    // Would block - but if HTTP/2, send SETTINGS first (edge-triggered epoll fix)
                    if (conn.recv_buffer.empty() && conn.protocol == Protocol::HTTP_2 &&
                        conn.h2_session) {
                        handle_http2(conn);  // Send SETTINGS with empty buffer
                    }
                    break;  // No more data available, exit loop
                }
                // Error or EOF - close connection
                handle_close(client_fd);
                return;
            }
        }

        // If we read nothing, return
        if (conn.recv_buffer.empty()) {
            return;
        }
    } else {
        // Raw read
        n = recv(client_fd, buffer, sizeof(buffer), 0);

        if (n <= 0) {
            // Error or EOF - close connection
            handle_close(client_fd);
            return;
        }

        // Append to buffer
        conn.recv_buffer.insert(conn.recv_buffer.end(), buffer, buffer + n);
    }

    // Detect protocol on first data (for cleartext connections)
    if (!conn.tls_enabled && conn.protocol == Protocol::Unknown) {
        if (http::is_http2_connection(conn.recv_buffer)) {
            // HTTP/2 connection (h2c with prior knowledge)
            conn.protocol = Protocol::HTTP_2;
            conn.h2_session = std::make_unique<http::H2Session>(true);  // server mode
        } else {
            // Assume HTTP/1.1
            conn.protocol = Protocol::HTTP_1_1;
        }
    }

    // Route to appropriate handler
    if (conn.protocol == Protocol::HTTP_2) {
        handle_http2(conn);
    } else {
        handle_http1(conn);
    }
}

void Server::handle_http1(Connection& conn) {
    // Process multiple pipelined requests if available (HTTP pipelining)
    while (true) {
        // Clear request and response objects before parsing (prevents data accumulation)
        conn.request = http::Request{};
        conn.response = http::Response{};
        conn.response_body.clear();

        // Get remaining data from cursor position
        auto remaining_data = std::span<const uint8_t>(conn.recv_buffer.data() + conn.recv_cursor,
                                                       conn.recv_buffer.size() - conn.recv_cursor);

        // Try to parse HTTP/1.1 request
        auto [result, consumed] = conn.parser.parse_request(remaining_data, conn.request);

        if (result == http::ParseResult::Complete) {
            // Copy ALL string_views to owned storage BEFORE buffer compaction
            // (path/uri/query/headers all point into recv_buffer which may be erased)

            // 1. Copy path/uri/query to owned storage
            conn.owned_request_path = conn.request.path;
            conn.owned_request_uri = conn.request.uri;
            conn.owned_request_query = conn.request.query;

            // 2. Copy headers to owned storage
            conn.request_header_storage.clear();
            conn.request_header_storage.reserve(conn.request.headers.size());
            for (const auto& header : conn.request.headers) {
                conn.request_header_storage.emplace_back(std::string(header.name),
                                                         std::string(header.value));
            }

            // 3. Fix string_views to point to owned storage
            conn.request.path = conn.owned_request_path;
            conn.request.uri = conn.owned_request_uri;
            conn.request.query = conn.owned_request_query;

            conn.request.headers.clear();
            conn.request.headers.reserve(conn.request_header_storage.size());
            for (const auto& [name, value] : conn.request_header_storage) {
                conn.request.headers.push_back({name, value});
            }

            // Process complete request
            bool keep_alive = process_request(conn);

            if (!keep_alive) {
                // Connection will be closed, don't touch conn after this
                return;
            }

            // Advance cursor (no expensive erase/memmove)
            conn.recv_cursor += consumed;

            // Compact buffer periodically to avoid unbounded growth
            if (conn.recv_cursor > 4096 && conn.recv_cursor > conn.recv_buffer.size() / 2) {
                conn.recv_buffer.erase(conn.recv_buffer.begin(),
                                       conn.recv_buffer.begin() + conn.recv_cursor);
                conn.recv_cursor = 0;
            }

            // Reset parser for next request (keep-alive)
            conn.parser.reset();

            // Continue loop to check if there's another pipelined request in buffer
        } else if (result == http::ParseResult::Error) {
            // Parse error - close connection
            handle_close(conn.fd);
            return;
        } else {
            // Incomplete - wait for more data
            return;
        }
    }
}

void Server::handle_http2(Connection& conn) {
    if (!conn.h2_session) {
        handle_close(conn.fd);
        return;
    }

    // Feed data to HTTP/2 session
    size_t consumed = 0;
    auto ec = conn.h2_session->recv(conn.recv_buffer, consumed);

    if (ec) {
        // Protocol error - close connection
        handle_close(conn.fd);
        return;
    }

    // Remove consumed bytes
    if (consumed > 0) {
        conn.recv_buffer.erase(conn.recv_buffer.begin(), conn.recv_buffer.begin() + consumed);
    }

    // Process completed streams
    auto active_streams = conn.h2_session->get_active_streams();

    for (auto* stream : active_streams) {
        if (stream->request_complete && !stream->response_complete) {
            // Process HTTP/2 request (similar to HTTP/1.1 but for stream)
            process_http2_stream(conn, *stream);
        }
    }

    // Send any pending data
    if (conn.h2_session->want_write()) {
        auto send_data = conn.h2_session->send_data();

        if (!send_data.empty()) {
            ssize_t sent;
            if (conn.tls_enabled) {
                sent = ssl_write_nonblocking(conn.ssl, send_data);
            } else {
                sent = send(conn.fd, send_data.data(), send_data.size(), 0);
            }

            if (sent > 0) {
                conn.h2_session->consume_send_buffer(sent);
            }
        }
    }

    // Check if connection should close
    if (conn.h2_session->should_close()) {
        handle_close(conn.fd);
    }
}

void Server::process_http2_stream(Connection& conn, http::H2Stream& stream) {
    // Match route using stream's request
    auto match = router_->match(stream.request.method, stream.request.path);

    // Build request context
    gateway::RequestContext ctx;
    ctx.request = &stream.request;
    ctx.response = &stream.response;
    ctx.correlation_id = logging::generate_correlation_id();
    ctx.route_match = match;
    ctx.client_ip = conn.remote_ip;
    ctx.client_port = conn.remote_port;
    ctx.start_time = std::chrono::steady_clock::now();

    // Set default response
    stream.response.status = http::StatusCode::NotFound;
    stream.response.version = http::Version::HTTP_2_0;

    if (match.handler_id.empty()) {
        // No route matched - 404
        stream.response.status = http::StatusCode::NotFound;
        stream.response_complete = true;
        auto ec = conn.h2_session->submit_response(stream.stream_id, stream.response);
        (void)ec;
    } else if (!match.upstream_name.empty()) {
        // Proxy to backend - ASYNC for HTTP/2

        // Execute request middleware pipeline first
        auto result = pipeline_->execute_request(ctx);

        if (result != gateway::MiddlewareResult::Continue) {
            // Middleware stopped request (e.g., rate limit, auth failure)
            // Response was set by middleware
            stream.response_complete = true;
            auto ec = conn.h2_session->submit_response(stream.stream_id, stream.response);
            (void)ec;
            return;
        }

        // Initiate async proxy - response will be sent later when backend responds
        // Copy stream.request to conn.request for proxy_to_backend (HTTP/1.1 compatibility)
        conn.request = stream.request;

        bool proxy_initiated = proxy_to_backend(conn, ctx);

        if (proxy_initiated && conn.backend_conn) {
            // HTTP/2 FIX: Move backend connection to per-stream map
            // This prevents stream state from being overwritten by concurrent requests
            int backend_fd = conn.backend_conn->backend_fd;
            auto stream_backend = std::move(conn.backend_conn);
            stream_backend->stream_id = stream.stream_id;
            conn.h2_stream_backends[stream.stream_id] = std::move(stream_backend);

            // CRITICAL: Update backend_connections_ map with (client_fd, stream_id)
            backend_connections_[backend_fd] = {conn.fd, stream.stream_id};

            // Don't submit response yet - will be done in handle_backend_event()
            return;
        } else {
            // Proxy initiation failed - return 502 immediately
            stream.response.status = http::StatusCode::BadGateway;
            stream.response_complete = true;
            auto ec = conn.h2_session->submit_response(stream.stream_id, stream.response);
            (void)ec;
        }
    } else {
        // Direct response
        stream.response.status = http::StatusCode::OK;
        stream.response_complete = true;
        auto ec = conn.h2_session->submit_response(stream.stream_id, stream.response);
        (void)ec;
    }
}

void Server::handle_close(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return;
    }

    Connection& conn = *it->second;

    // Clean up backend connection if exists (HTTP/1.1)
    if (conn.backend_conn) {
        int backend_fd = conn.backend_conn->backend_fd;

        // Remove from backend_connections map
        backend_connections_.erase(backend_fd);

        // Remove from backend epoll (if added)
#ifdef __linux__
        epoll_ctl(backend_epoll_fd_, EPOLL_CTL_DEL, backend_fd, nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__)
        struct kevent kev;
        EV_SET(&kev, backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(backend_epoll_fd_, &kev, 1, nullptr, 0, nullptr);
        EV_SET(&kev, backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(backend_epoll_fd_, &kev, 1, nullptr, 0, nullptr);
#endif

        // Close backend socket
        close_fd(backend_fd);

        // Reset will be automatic when Connection is destroyed
    }

    // HTTP/2 FIX: Clean up all HTTP/2 stream backend connections
    for (auto& [stream_id, stream_backend] : conn.h2_stream_backends) {
        if (stream_backend) {
            int backend_fd = stream_backend->backend_fd;

            // Remove from backend_connections map
            backend_connections_.erase(backend_fd);

            // Remove from backend epoll (if added)
#ifdef __linux__
            epoll_ctl(backend_epoll_fd_, EPOLL_CTL_DEL, backend_fd, nullptr);
#elif defined(__APPLE__) || defined(__FreeBSD__)
            struct kevent kev;
            EV_SET(&kev, backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(backend_epoll_fd_, &kev, 1, nullptr, 0, nullptr);
            EV_SET(&kev, backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(backend_epoll_fd_, &kev, 1, nullptr, 0, nullptr);
#endif

            // Close backend socket
            close_fd(backend_fd);
        }
    }
    // Map will be cleared when Connection is destroyed

    // Clean up SSL connection if exists
    ssl_connections_.erase(client_fd);

    close_fd(it->second->fd);
    connections_.erase(it);
}

bool Server::process_request(Connection& conn) {
    // Internal endpoints (/_health, /metrics) moved to separate admin server
    // This ensures they're not exposed on public-facing port 8080
    // Admin server runs on port 9090 (configurable via metrics.port)

    // Match route (uses SIMD-accelerated router for longer paths)
    auto match = router_->match(conn.request.method, conn.request.path);

    // Build request context
    gateway::RequestContext ctx;
    ctx.request = &conn.request;
    ctx.response = &conn.response;
    ctx.correlation_id = logging::generate_correlation_id();
    ctx.route_match = match;
    ctx.client_ip = conn.remote_ip;
    ctx.client_port = conn.remote_port;
    ctx.start_time = std::chrono::steady_clock::now();

    // Set default response
    conn.response.status = http::StatusCode::NotFound;
    conn.response.version = http::Version::HTTP_1_1;

    // Determine if client wants keep-alive (HTTP/1.1 defaults to keep-alive)
    bool client_wants_keepalive = true;
    if (conn.request.version == http::Version::HTTP_1_0) {
        client_wants_keepalive = false;  // HTTP/1.0 defaults to close
    }

    // Check Connection header
    for (const auto& h : conn.request.headers) {
        if (h.name == "Connection" || h.name == "connection") {
            if (h.value.find("close") != std::string::npos) {
                client_wants_keepalive = false;
            } else if (h.value.find("keep-alive") != std::string::npos) {
                client_wants_keepalive = true;
            }
            break;
        }
    }

    // Execute request middleware
    auto middleware_result = pipeline_->execute_request(ctx);

    if (middleware_result == gateway::MiddlewareResult::Stop) {
        // Middleware handled the request (e.g., auth failure, rate limit)
        // Response status/body already set by middleware
        send_response(conn, client_wants_keepalive);
        return client_wants_keepalive;
    }

    if (middleware_result == gateway::MiddlewareResult::Error) {
        // Middleware error
        conn.response.status = http::StatusCode::InternalServerError;
        send_response(conn, client_wants_keepalive);
        return client_wants_keepalive;
    }

    // Check if middleware set upstream (ProxyMiddleware sets ctx.upstream)
    if (ctx.upstream != nullptr) {
        // Save keep-alive decision for async response (will be used by handle_backend_event)
        conn.keep_alive = client_wants_keepalive;

        // Proxy to backend (async operation)
        bool success = proxy_to_backend(conn, ctx);
        if (!success) {
            // Proxying failed synchronously, return 502 Bad Gateway
            conn.response.status = http::StatusCode::BadGateway;
            send_response(conn, client_wants_keepalive);
            return client_wants_keepalive;
        }

        // Async proxy initiated successfully
        // Response will be sent later by handle_backend_event()
        // Return true to keep connection alive for async response
        return true;
    }

    // No upstream configured - return direct response or 404
    if (match.handler_id.empty()) {
        conn.response.status = http::StatusCode::NotFound;
    } else {
        conn.response.status = http::StatusCode::OK;
    }
    send_response(conn, client_wants_keepalive);
    return client_wants_keepalive;
}

void Server::send_response(Connection& conn, bool keep_alive) {
    // Build response string
    std::string response_str;

    // Pre-reserve capacity to avoid allocations (estimate: 200 bytes headers + body size)
    size_t body_size = conn.response.body.empty() ? 0 : conn.response.body.size();
    size_t estimated_size = 200 + body_size;
    // Estimate header sizes using all_headers iterator (backend + middleware)
    for (auto it = conn.response.all_headers_begin(); it != conn.response.all_headers_end(); ++it) {
        auto [name, value] = *it;
        estimated_size += name.size() + value.size() + 4;  // ": \r\n"
    }
    response_str.reserve(estimated_size);

    // Status line with reason phrase
    response_str += "HTTP/1.1 ";
    response_str += std::to_string(static_cast<int>(conn.response.status));
    response_str += " ";
    response_str += http::to_reason_phrase(conn.response.status);
    response_str += "\r\n";

    // Forward all headers (backend + middleware, except Content-Length and Connection)
    for (auto it = conn.response.all_headers_begin(); it != conn.response.all_headers_end(); ++it) {
        auto [name, value] = *it;
        // Skip headers we'll add ourselves
        if (name == "Content-Length" || name == "content-length" ||
            name == "Connection" || name == "connection") {
            continue;
        }
        response_str += name;
        response_str += ": ";
        response_str += value;
        response_str += "\r\n";
    }

    // Content-Length header (calculated from actual body size)
    response_str += "Content-Length: ";
    response_str += std::to_string(body_size);
    response_str += "\r\n";

    // Connection header based on keep_alive parameter
    if (keep_alive) {
        response_str += "Connection: keep-alive\r\n";
    } else {
        response_str += "Connection: close\r\n";
    }
    response_str += "\r\n";

    // Add body if present
    if (!conn.response.body.empty()) {
        response_str.append(reinterpret_cast<const char*>(conn.response.body.data()),
                            conn.response.body.size());
    }

    // Send (use TLS if enabled)
    if (conn.tls_enabled) {
        (void)ssl_write_nonblocking(
            conn.ssl,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(response_str.data()),
                                     response_str.size()));
    } else {
        send(conn.fd, response_str.data(), response_str.size(), 0);
    }

    // Close connection only if not keep-alive
    if (!keep_alive) {
        handle_close(conn.fd);
    }
}

bool Server::proxy_to_backend(Connection& conn, gateway::RequestContext& ctx) {
    // Async proxy - return immediately, handle in backend_epoll

    // Get upstream from context (set by ProxyMiddleware)
    auto* upstream = ctx.upstream;
    if (!upstream) {
        return false;
    }

    // Select backend using load balancer with circuit breaker check
    const auto& backends = upstream->backends();
    if (backends.empty()) {
        return false;
    }

    // Find first available backend (health + circuit breaker check)
    const gateway::Backend* selected_backend = nullptr;
    for (const auto& backend : backends) {
        if (backend.can_accept_connection()) {
            selected_backend = &backend;
            break;
        }
    }

    if (!selected_backend) {
        // No healthy/available backends (all unhealthy or circuit breakers open)
        return false;
    }

    const auto& backend = *selected_backend;

    // Create async backend connection
    conn.backend_conn = std::make_unique<BackendConnection>();
    conn.backend_conn->client_fd = conn.fd;
    conn.backend_conn->upstream_name = std::string(upstream->name());
    conn.backend_conn->backend_host = backend.host;
    conn.backend_conn->backend_port = backend.port;

    // Store timing and metadata for response middleware
    conn.backend_conn->start_time = ctx.start_time;
    conn.backend_conn->metadata = std::move(ctx.metadata);
    conn.backend_conn->metadata["correlation_id"] = ctx.correlation_id;
    conn.backend_conn->route_match = ctx.route_match;

    // Preserve request for response middleware (HTTP/1.1 keep-alive safety)
    // conn.request will be overwritten by next pipelined request, so copy it now

    // 1. Copy path/uri/query to owned storage (string_views point into recv_buffer)
    conn.backend_conn->owned_path = conn.request.path;
    conn.backend_conn->owned_uri = conn.request.uri;
    conn.backend_conn->owned_query = conn.request.query;

    // 2. Copy headers to owned storage
    conn.backend_conn->request_header_storage = conn.request_header_storage;

    // 3. Copy request struct (copies all fields including string_views)
    conn.backend_conn->preserved_request = conn.request;

    // 4. Fix string_views to point to owned storage (not recv_buffer!)
    conn.backend_conn->preserved_request.path = conn.backend_conn->owned_path;
    conn.backend_conn->preserved_request.uri = conn.backend_conn->owned_uri;
    conn.backend_conn->preserved_request.query = conn.backend_conn->owned_query;

    // 5. Fix headers to point to owned storage
    conn.backend_conn->preserved_request.headers.clear();
    conn.backend_conn->preserved_request.headers.reserve(
        conn.backend_conn->request_header_storage.size());
    for (const auto& [name, value] : conn.backend_conn->request_header_storage) {
        conn.backend_conn->preserved_request.headers.push_back({name, value});
    }

    // Try to acquire from pool first
    conn.backend_conn->backend_fd = upstream->backend_pool().acquire(backend.host, backend.port);

    if (conn.backend_conn->backend_fd < 0) {
        // Pool empty - initiate non-blocking connect
        conn.backend_conn->backend_fd = connect_to_backend_async(backend.host, backend.port);
        if (conn.backend_conn->backend_fd < 0) {
            conn.backend_conn.reset();
            return false;
        }
        // Mark as needing to connect
        conn.backend_conn->connect_pending = true;
    } else {
        // Acquired from pool - connection is established but removed from epoll
        conn.backend_conn->connect_pending = false;

        // Reset state from previous request
        conn.backend_conn->send_cursor = 0;
        conn.backend_conn->send_buffer.clear();
        conn.backend_conn->recv_buffer.clear();
        conn.backend_conn->send_pending = false;
        conn.backend_conn->recv_pending = false;
    }

    // Build request and store in send buffer (use transformed path/query from metadata if present)
    std::string request_str = build_backend_request(conn.request, conn.backend_conn->metadata);
    conn.backend_conn->send_buffer.assign(
        reinterpret_cast<const uint8_t*>(request_str.data()),
        reinterpret_cast<const uint8_t*>(request_str.data() + request_str.size()));

    // Mark as pending send (connect_pending was set above based on pool acquisition)
    conn.backend_conn->send_pending = true;

    // Add to backend epoll (both new and pooled connections need this)
    // - New connections: Never been in epoll, need EPOLL_CTL_ADD
    // - Pooled connections: Were removed from epoll when pooled, need EPOLL_CTL_ADD again
    if (!add_backend_to_epoll(conn.backend_conn->backend_fd, EPOLLOUT | EPOLLIN)) {
        close_fd(conn.backend_conn->backend_fd);
        conn.backend_conn.reset();
        return false;
    }

    // Register in backend_connections map (client_fd, stream_id=-1 for HTTP/1.1)
    backend_connections_[conn.backend_conn->backend_fd] = {conn.fd, -1};

    // Return true - request will be handled asynchronously
    // handle_backend_event() will be called when backend socket is ready
    return true;
}

int Server::connect_to_backend(const std::string& host, uint16_t port) {
    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Resolve hostname
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try direct IP first (fastest path)
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Check DNS cache before doing expensive resolution
        auto cache_it = dns_cache_.find(host);
        if (cache_it != dns_cache_.end()) {
            // Cache hit - reuse resolved address
            addr = cache_it->second;
            addr.sin_port = htons(port);  // Port might differ
        } else {
            // Cache miss - perform DNS resolution
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
                close_fd(sockfd);
                return -1;
            }

            addr = *reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            addr.sin_port = htons(port);
            freeaddrinfo(result);

            // Store in cache for future connections
            dns_cache_[host] = addr;
        }
    }

    // Connect (blocking for MVP - TODO: non-blocking + io_uring)
    if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_fd(sockfd);
        return -1;
    }

    // Enable TCP_NODELAY to reduce latency (disable Nagle's algorithm)
    // This is critical for API gateway workloads with small messages
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    return sockfd;
}

int Server::connect_to_backend_async(const std::string& host, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Make socket non-blocking BEFORE connect
    if (auto ec = set_nonblocking(sockfd); ec) {
        close_fd(sockfd);
        return -1;
    }

    // Enable TCP_NODELAY immediately
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    // Resolve address (same as blocking version)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try direct IP first
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Check DNS cache
        auto cache_it = dns_cache_.find(host);
        if (cache_it != dns_cache_.end()) {
            addr = cache_it->second;
            addr.sin_port = htons(port);
        } else {
            // DNS resolution (still blocking for now - TODO: async DNS)
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
                close_fd(sockfd);
                return -1;
            }

            addr = *reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            addr.sin_port = htons(port);
            freeaddrinfo(result);

            dns_cache_[host] = addr;
        }
    }

    // Non-blocking connect - will return EINPROGRESS
    int result = connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    if (result < 0) {
        // EINPROGRESS is expected for non-blocking connect
        if (errno != EINPROGRESS) {
            close_fd(sockfd);
            return -1;
        }
        // Connection in progress - epoll will notify when ready
    }

    return sockfd;
}

std::string Server::build_backend_request(
    const http::Request& request, const titan::core::fast_map<std::string, std::string>& metadata) {
    std::string req;

    // Use transformed path/query from metadata if present (from TransformMiddleware)
    std::string_view actual_path = request.path;
    std::string_view actual_query = request.query;

    auto transformed_path_it = metadata.find("transformed_path");
    if (transformed_path_it != metadata.end()) {
        actual_path = transformed_path_it->second;
    }

    auto transformed_query_it = metadata.find("transformed_query");
    if (transformed_query_it != metadata.end()) {
        actual_query = transformed_query_it->second;
    }

    // Calculate size to avoid reallocation
    // Format: METHOD path[?query] HTTP/1.1\r\n + headers + \r\n + body
    size_t estimated_size = kRequestLineBaseSize;
    estimated_size += actual_path.size();
    estimated_size += actual_query.empty() ? 0 : (kQuerySeparatorSize + actual_query.size());

    // Estimate headers size
    for (const auto& header : request.headers) {
        if (header.name != "Connection" && header.name != "connection") {
            estimated_size += header.name.size() + header.value.size() + kHeaderSeparatorSize;
        }
    }
    estimated_size += kRequestHeaderMargin;  // Connection, Host headers + safety
    estimated_size += request.body.size();

    req.reserve(estimated_size);

    // Request line: METHOD path HTTP/1.1
    req += http::to_string(request.method);
    req += " ";
    req += actual_path;
    if (!actual_query.empty()) {
        req += "?";
        req += actual_query;
    }
    req += " HTTP/1.1\r\n";

    // Build set of headers to remove (from TransformMiddleware)
    std::unordered_set<std::string_view> headers_to_remove;
    for (const auto& [key, value] : metadata) {
        if (key.starts_with("header_remove:")) {
            headers_to_remove.insert(key.substr(14));  // Skip "header_remove:"
        }
    }

    // Forward headers (except Connection header and removed headers)
    bool has_host = false;
    for (const auto& header : request.headers) {
        // Skip Connection header - we want keep-alive for backend pooling
        if (header.name == "Connection" || header.name == "connection") {
            continue;
        }

        // Skip headers marked for removal by TransformMiddleware
        if (headers_to_remove.count(header.name) > 0) {
            continue;
        }

        if (header.name == "Host" || header.name == "host") {
            has_host = true;
        }

        // Check if this header should be modified
        std::string modify_key = "header_modify:" + std::string(header.name);
        auto modify_it = metadata.find(modify_key);
        if (modify_it != metadata.end()) {
            // Use modified value
            req += header.name;
            req += ": ";
            req += modify_it->second;
            req += "\r\n";
        } else {
            // Use original value
            req += header.name;
            req += ": ";
            req += header.value;
            req += "\r\n";
        }
    }

    // Add new headers from TransformMiddleware
    for (const auto& [key, value] : metadata) {
        if (key.starts_with("header_add:")) {
            std::string_view header_name = std::string_view(key).substr(11);  // Skip "header_add:"
            req += header_name;
            req += ": ";
            req += value;
            req += "\r\n";
        }
    }

    // Ensure Host header exists (required for HTTP/1.1)
    if (!has_host) {
        req += "Host: backend\r\n";
    }

    // Always use keep-alive for backend connections (enables connection pooling)
    req += "Connection: keep-alive\r\n";

    // End headers
    req += "\r\n";

    // Add body if present
    if (!request.body.empty()) {
        req.append(reinterpret_cast<const char*>(request.body.data()), request.body.size());
    }

    return req;
}

bool Server::receive_backend_response(int backend_fd, http::Response& response,
                                      std::vector<uint8_t>& buffer) {
    buffer.clear();

    // Pre-reserve larger buffer to avoid multiple reallocations
    buffer.reserve(kBackendResponseBufferSize);

    http::Parser parser;
    uint8_t chunk[kBackendReadChunkSize];

    // Read with blocking until we get some data
    ssize_t n = recv(backend_fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
        return false;  // Connection error or closed
    }

    // Append to buffer (uses memcpy internally, faster than insert with iterators)
    buffer.insert(buffer.end(), chunk, chunk + n);

    // Continue reading all immediately available data (non-blocking)
    // This ensures we read the complete response before parsing
    while (true) {
        n = recv(backend_fd, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available right now - try to parse
                break;
            }
            // Real error
            return false;
        }
        if (n == 0) {
            // Connection closed
            break;
        }

        buffer.insert(buffer.end(), chunk, chunk + n);

        // Safety limit to prevent unbounded memory growth
        if (buffer.size() > kBackendMaxResponseSize) {
            return false;
        }
    }

    // Parse the response
    auto [result, consumed] = parser.parse_response(std::span<const uint8_t>(buffer), response);

    if (result == http::ParseResult::Complete) {
        return true;
    }

    // If incomplete, we might need more data - wait a bit for slow responses
    // Set timeout and try reading more
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(backend_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // If still incomplete, keep reading until complete or timeout
    while (result == http::ParseResult::Incomplete) {
        n = recv(backend_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;  // Timeout or connection closed
        }

        buffer.insert(buffer.end(), chunk, chunk + n);

        // Reset parser and re-parse the entire buffer from scratch
        // Parser maintains state, so we must reset before re-parsing accumulated data
        parser.reset();
        auto [new_result, new_consumed] =
            parser.parse_response(std::span<const uint8_t>(buffer), response);
        result = new_result;

        if (result == http::ParseResult::Complete) {
            return true;
        } else if (result == http::ParseResult::Error) {
            return false;
        }

        // Safety limit
        if (buffer.size() > 100 * 1024 * 1024) {
            return false;
        }
    }

    return result == http::ParseResult::Complete;
}

// Backend event handling for dual epoll pattern
void Server::handle_backend_event(int backend_fd, bool readable, bool writable, bool error) {
    auto it = backend_connections_.find(backend_fd);
    if (it == backend_connections_.end()) {
        return;
    }

    // Get client_fd and stream_id from map (no longer storing raw pointers)
    int client_fd = it->second.first;
    int32_t stream_id = it->second.second;

    auto conn_it = connections_.find(client_fd);
    if (conn_it == connections_.end()) {
        // Client connection closed, cleanup backend
        backend_connections_.erase(it);
        close_fd(backend_fd);
        return;
    }

    Connection& client_conn = *conn_it->second;

    // Get the actual BackendConnection pointer from the appropriate container
    BackendConnection* backend_conn = nullptr;
    if (stream_id == -1) {
        // HTTP/1.1: Get from single backend connection
        backend_conn = client_conn.backend_conn.get();
    } else {
        // HTTP/2: Get from per-stream backends
        auto stream_it = client_conn.h2_stream_backends.find(stream_id);
        if (stream_it != client_conn.h2_stream_backends.end()) {
            backend_conn = stream_it->second.get();
        }
    }

    if (!backend_conn) {
        // Backend connection was already cleaned up, remove from map
        backend_connections_.erase(it);
        close_fd(backend_fd);
        return;
    }

    // Handle error
    if (error) {
        // Backend connection failed or closed
        backend_connections_.erase(it);
        close_fd(backend_fd);

        // HTTP/2 FIX: Remove from correct location based on protocol
        int32_t stream_id = backend_conn->stream_id;
        if (stream_id >= 0) {
            // HTTP/2: Remove from per-stream backends
            client_conn.h2_stream_backends.erase(stream_id);
        } else {
            // HTTP/1.1: Remove from single backend connection
            client_conn.backend_conn.reset();
        }

        // Send error response to client
        client_conn.response.status = http::StatusCode::BadGateway;
        client_conn.response.reason_phrase = "Bad Gateway";
        client_conn.response.headers.clear();
        client_conn.response_body.clear();
        send_response(client_conn, false);
        return;
    }

    // Handle connect completion (EPOLLOUT fires when connect finishes)
    if (writable && backend_conn->connect_pending) {
        // Check if connect succeeded or failed
        int connect_error = 0;
        socklen_t len = sizeof(connect_error);
        if (getsockopt(backend_fd, SOL_SOCKET, SO_ERROR, &connect_error, &len) < 0) {
            // getsockopt failed

            // Record circuit breaker failure before cleanup
            auto* upstream = upstream_manager_->get_upstream(backend_conn->upstream_name);
            if (upstream) {
                for (auto& backend : upstream->backends()) {
                    if (backend.host == backend_conn->backend_host &&
                        backend.port == backend_conn->backend_port) {
                        if (backend.circuit_breaker) {
                            backend.circuit_breaker->record_failure();
                        }
                        break;
                    }
                }
            }

            backend_connections_.erase(it);
            close_fd(backend_fd);

            // HTTP/2 FIX: Remove from correct location
            int32_t stream_id = backend_conn->stream_id;
            if (stream_id >= 0) {
                client_conn.h2_stream_backends.erase(stream_id);
            } else {
                client_conn.backend_conn.reset();
            }

            client_conn.response.status = http::StatusCode::BadGateway;
            client_conn.response.reason_phrase = "Bad Gateway";
            client_conn.response.headers.clear();  // Clear any residual headers from middleware
                                                   // client_conn.response_body.clear();
            send_response(client_conn, false);
            return;
        }

        if (connect_error != 0) {
            // Connect failed

            // Record circuit breaker failure before cleanup
            auto* upstream = upstream_manager_->get_upstream(backend_conn->upstream_name);
            if (upstream) {
                for (auto& backend : upstream->backends()) {
                    if (backend.host == backend_conn->backend_host &&
                        backend.port == backend_conn->backend_port) {
                        if (backend.circuit_breaker) {
                            backend.circuit_breaker->record_failure();
                        }
                        break;
                    }
                }
            }

            backend_connections_.erase(it);
            close_fd(backend_fd);

            // HTTP/2 FIX: Remove from correct location
            int32_t stream_id = backend_conn->stream_id;
            if (stream_id >= 0) {
                client_conn.h2_stream_backends.erase(stream_id);
            } else {
                client_conn.backend_conn.reset();
            }

            client_conn.response.status = http::StatusCode::BadGateway;
            client_conn.response.reason_phrase = "Bad Gateway";
            client_conn.response.headers.clear();  // Clear any residual headers from middleware
                                                   // client_conn.response_body.clear();
            send_response(client_conn, false);
            return;
        }

        // Connect succeeded!
        backend_conn->connect_pending = false;
        // send_pending is still true, will be handled below
    }

    // Handle writable (can send request to backend)
    if (writable && !backend_conn->connect_pending && backend_conn->send_pending) {
        ssize_t sent =
            send(backend_fd, backend_conn->send_buffer.data() + backend_conn->send_cursor,
                 backend_conn->send_buffer.size() - backend_conn->send_cursor, MSG_NOSIGNAL);

        if (sent > 0) {
            backend_conn->send_cursor += sent;
            if (backend_conn->send_cursor >= backend_conn->send_buffer.size()) {
                // All data sent, now wait for response
                backend_conn->send_pending = false;
                backend_conn->recv_pending = true;
            }
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Send failed

            // Record circuit breaker failure before cleanup
            auto* upstream = upstream_manager_->get_upstream(backend_conn->upstream_name);
            if (upstream) {
                for (auto& backend : upstream->backends()) {
                    if (backend.host == backend_conn->backend_host &&
                        backend.port == backend_conn->backend_port) {
                        if (backend.circuit_breaker) {
                            backend.circuit_breaker->record_failure();
                        }
                        break;
                    }
                }
            }

            backend_connections_.erase(it);
            close_fd(backend_fd);

            // HTTP/2 FIX: Remove from correct location
            int32_t stream_id = backend_conn->stream_id;
            if (stream_id >= 0) {
                client_conn.h2_stream_backends.erase(stream_id);
            } else {
                client_conn.backend_conn.reset();
            }

            client_conn.response.status = http::StatusCode::BadGateway;
            client_conn.response.reason_phrase = "Bad Gateway";
            client_conn.response.headers.clear();  // Clear any residual headers from middleware
                                                   // client_conn.response_body.clear();
            send_response(client_conn, false);
        }
    }

    // Handle readable (backend response available)
    if (readable && backend_conn->recv_pending) {
        // CRITICAL FIX: Loop reading until response complete or EAGAIN
        // Large responses (>8KB) require multiple recv() calls
        bool done_reading = false;
        bool should_send_error = false;

        // Read loop: accumulate data in recv_buffer without parsing
        while (!done_reading) {
            uint8_t chunk[8192];
            ssize_t n = recv(backend_fd, chunk, sizeof(chunk), MSG_DONTWAIT);

            if (n > 0) {
                // Append data to buffer
                backend_conn->recv_buffer.insert(backend_conn->recv_buffer.end(), chunk, chunk + n);
                // Continue reading more data (don't parse yet - would create invalid string_views!)
            } else if (n == 0) {
                // Backend closed connection
                done_reading = true;
            } else {
                // n < 0 - check if EAGAIN (no more data available) or real error
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No more data available now - try parsing what we have
                    done_reading = true;  // Exit read loop and try parsing
                } else {
                    // Real error - send 502
                    should_send_error = true;
                    done_reading = true;
                }
            }
        }

        // Now try parsing the complete buffer (AFTER all reading is done)
        bool response_complete = false;
        http::Response response;

        if (!should_send_error && !backend_conn->recv_buffer.empty()) {
            http::Parser parser;
            auto [result, consumed] = parser.parse_response(
                std::span<const uint8_t>(backend_conn->recv_buffer), response);

            if (result == http::ParseResult::Complete) {
                response_complete = true;
            } else if (result == http::ParseResult::Error) {
                should_send_error = true;
            } else {
                // Incomplete - wait for next epoll event to read more data
                return;  // Don't process response yet, wait for more data
            }
        }

        // Handle error cases - send 502 Bad Gateway to client
        if (should_send_error) {
            // Send error response using existing client_conn reference
            client_conn.response.status = http::StatusCode::BadGateway;
            client_conn.response.reason_phrase = "Bad Gateway";
            client_conn.response.headers.clear();
            client_conn.response.body = std::span<const uint8_t>();
            send_response(client_conn, false);  // Close connection after error

            // Cleanup backend connection
            close_fd(backend_fd);
            (void)remove_backend_from_epoll(backend_fd);
            backend_connections_.erase(it);
            return;
        }

        if (response_complete) {
            // Response complete - copy response to client connection

            // Copy status and reason
            client_conn.response.status = response.status;
            client_conn.response.reason_phrase = response.reason_phrase;

            // CRITICAL: Convert headers from string_views (pointing into backend recv_buffer)
            // to owned strings stored in Connection, BEFORE we reset backend_conn
            client_conn.response_header_storage.clear();
            client_conn.response_header_storage.reserve(response.headers.size());
            for (const auto& h : response.headers) {
                client_conn.response_header_storage.emplace_back(std::string(h.name),
                                                                 std::string(h.value));
            }

            // Now create Headers with string_views pointing to our owned storage
            client_conn.response.headers.clear();
            client_conn.response.headers.reserve(client_conn.response_header_storage.size());
            for (const auto& [name, value] : client_conn.response_header_storage) {
                client_conn.response.headers.push_back({name, value});
            }

            // Copy body to owned buffer
            client_conn.response_body.assign(response.body.begin(), response.body.end());
            client_conn.response.body = client_conn.response_body;

            // Execute response middleware
            gateway::ResponseContext resp_ctx;
            resp_ctx.request =
                &backend_conn
                     ->preserved_request;  // Use preserved request (not current conn.request)
            resp_ctx.response = &client_conn.response;
            resp_ctx.correlation_id = backend_conn->metadata["correlation_id"];
            resp_ctx.route_match = backend_conn->route_match;
            resp_ctx.client_ip = client_conn.remote_ip;
            resp_ctx.client_port = client_conn.remote_port;
            resp_ctx.start_time = backend_conn->start_time;
            resp_ctx.metadata = backend_conn->metadata;

            // Populate backend for circuit breaker feedback
            auto* upstream = upstream_manager_->get_upstream(backend_conn->upstream_name);
            if (upstream) {
                // Find the backend by host:port
                for (auto& backend : upstream->backends()) {
                    if (backend.host == backend_conn->backend_host &&
                        backend.port == backend_conn->backend_port) {
                        resp_ctx.backend = const_cast<gateway::Backend*>(&backend);
                        break;
                    }
                }
            }

            (void)pipeline_->execute_response(resp_ctx);

            // Return backend connection to pool (or close if not keep-alive)
            // CRITICAL: Remove from epoll BEFORE returning to pool!
            // Reuse upstream from above (already looked up for circuit breaker)
            if (upstream) {
                // Remove from epoll so pooled connections don't generate spurious events
                (void)remove_backend_from_epoll(backend_fd);

                // Check if backend wants to close the connection
                bool should_close = false;
                for (const auto& [name, value] : response.headers) {
                    if (name == "Connection" || name == "connection") {
                        if (value == "close" || value == "Close") {
                            should_close = true;
                            break;
                        }
                    }
                }

                if (should_close) {
                    // Backend sent Connection: close - don't pool, just close
                    close_fd(backend_fd);
                } else {
                    // Safe to return to pool for reuse
                    upstream->backend_pool().release(backend_fd, backend_conn->backend_host,
                                                     backend_conn->backend_port);
                }
            } else {
                // Upstream not found - just close
                close_fd(backend_fd);
            }

            // Cleanup backend connection
            backend_connections_.erase(it);
            int32_t stream_id = backend_conn->stream_id;  // Save before reset

            // HTTP/2 FIX: Remove from correct location based on protocol
            if (stream_id >= 0) {
                // HTTP/2: Remove from per-stream backends
                client_conn.h2_stream_backends.erase(stream_id);
            } else {
                // HTTP/1.1: Remove from single backend connection
                client_conn.backend_conn.reset();
            }

            // Send response to client
            if (client_conn.protocol == Protocol::HTTP_2) {
                // HTTP/2 - submit response to H2 session
                if (client_conn.h2_session && stream_id >= 0) {
                    auto* stream = client_conn.h2_session->get_stream(stream_id);
                    if (stream) {
                        // Copy response (headers contain string_views that must be converted to
                        // owned strings)
                        stream->response.status = client_conn.response.status;
                        stream->response.reason_phrase = client_conn.response.reason_phrase;

                        // Store headers in persistent storage, then create views to them
                        // IMPORTANT: Use all_headers iterator to include BOTH backend and middleware headers
                        stream->response_header_storage.clear();
                        stream->response.headers.clear();

                        // Iterate over all headers (backend + middleware)
                        for (auto it = client_conn.response.all_headers_begin();
                             it != client_conn.response.all_headers_end(); ++it) {
                            auto [name, value] = *it;
                            stream->response_header_storage.emplace_back(std::string(name),
                                                                         std::string(value));
                            const auto& stored = stream->response_header_storage.back();
                            stream->response.headers.push_back({stored.first, stored.second});
                        }

                        // Copy body
                        stream->response_body = std::move(client_conn.response_body);
                        stream->response.body = stream->response_body;
                        stream->response_complete = true;

                        // Filter out HTTP/1.1-specific headers forbidden in HTTP/2
                        // Per RFC 7540 Section 8.1.2: connection-specific headers must not be
                        // included Also filter out empty headers
                        auto& headers = stream->response.headers;
                        headers.erase(std::remove_if(headers.begin(), headers.end(),
                                                     [](const http::Header& h) {
                                                         // Remove empty headers
                                                         if (h.name.empty() || h.value.empty()) {
                                                             return true;
                                                         }
                                                         std::string name_lower(h.name);
                                                         std::transform(
                                                             name_lower.begin(), name_lower.end(),
                                                             name_lower.begin(), ::tolower);
                                                         return name_lower == "connection" ||
                                                                name_lower == "keep-alive" ||
                                                                name_lower == "proxy-connection" ||
                                                                name_lower == "transfer-encoding" ||
                                                                name_lower == "upgrade";
                                                     }),
                                      headers.end());

                        // Submit response to HTTP/2 session
                        auto ec =
                            client_conn.h2_session->submit_response(stream_id, stream->response);
                        (void)ec;  // Suppress unused variable warning

                        // Serialize and send HTTP/2 frames
                        auto data = client_conn.h2_session->send_data();
                        if (!data.empty()) {
                            ssize_t sent;
                            if (client_conn.tls_enabled) {
                                sent = ssl_write_nonblocking(client_conn.ssl, data);
                            } else {
                                sent = send(client_conn.fd, data.data(), data.size(), 0);
                            }

                            if (sent > 0) {
                                client_conn.h2_session->consume_send_buffer(sent);
                            }
                        }

                        // CRITICAL FIX for TLS HTTP/2 multiplexing:
                        // After sending a response, check if there's more client data buffered in SSL.
                        // This handles edge-triggered epoll + SSL buffering: when multiple HTTP/2 requests
                        // arrive in the same TLS record, subsequent requests sit in SSL's internal buffer
                        // and epoll won't fire again (data already decrypted, not at socket layer).
                        // Without this, the second request on a multiplexed connection hangs forever.
                        if (client_conn.tls_enabled) {
                            while (SSL_pending(client_conn.ssl) > 0) {
                                // Drain SSL internal buffer
                                size_t available =
                                    client_conn.recv_buffer.capacity() - client_conn.recv_buffer.size();
                                if (available == 0) {
                                    client_conn.recv_buffer.reserve(client_conn.recv_buffer.capacity() +
                                                                    8192);
                                    available =
                                        client_conn.recv_buffer.capacity() - client_conn.recv_buffer.size();
                                }

                                int n = SSL_read(client_conn.ssl,
                                                 client_conn.recv_buffer.data() +
                                                     client_conn.recv_buffer.size(),
                                                 available);
                                if (n > 0) {
                                    client_conn.recv_buffer.resize(client_conn.recv_buffer.size() + n);
                                } else {
                                    break;  // No more data or would block
                                }
                            }

                            // Process any buffered client frames
                            if (!client_conn.recv_buffer.empty()) {
                                handle_http2(client_conn);
                            }
                        }
                    }
                }
            } else {
                // HTTP/1.1 - use existing send_response
                send_response(client_conn, client_conn.keep_alive);
            }
        }
        // If !response_complete: either EAGAIN (wait for more data) or error (already handled in
        // loop)
    }
}

void Server::process_backend_operations() {
    // This method is called periodically to process any pending backend operations
    // For now, it's a placeholder - most processing happens in handle_backend_event()
    // In the future, this could handle timeouts, retries, etc.
}

bool Server::add_backend_to_epoll(int backend_fd, uint32_t events) {
#ifdef __linux__
    epoll_event ev{};
    ev.events = events | EPOLLET;  // Edge-triggered
    ev.data.fd = backend_fd;
    return epoll_ctl(backend_epoll_fd_, EPOLL_CTL_ADD, backend_fd, &ev) == 0;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // For kqueue, we need to add separate kevents for read and write
    struct kevent kevs[2];
    int nchanges = 0;

    if (events & EPOLLIN) {
        EV_SET(&kevs[nchanges++], backend_fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
               nullptr);
    }
    if (events & EPOLLOUT) {
        EV_SET(&kevs[nchanges++], backend_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
               nullptr);
    }

    if (nchanges > 0) {
        return kevent(backend_epoll_fd_, kevs, nchanges, nullptr, 0, nullptr) == 0;
    }
    return false;
#else
    return false;
#endif
}

bool Server::remove_backend_from_epoll(int backend_fd) {
#ifdef __linux__
    // On Linux, EPOLL_CTL_DEL doesn't need an event structure (can be nullptr)
    return epoll_ctl(backend_epoll_fd_, EPOLL_CTL_DEL, backend_fd, nullptr) == 0;
#elif defined(__APPLE__) || defined(__FreeBSD__)
    // For kqueue, we need to delete both READ and WRITE filters
    struct kevent kevs[2];
    EV_SET(&kevs[0], backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&kevs[1], backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    // It's OK if some filters don't exist (returns error but we ignore it)
    kevent(backend_epoll_fd_, kevs, 2, nullptr, 0, nullptr);
    return true;  // Always return true since partial success is OK
#else
    return false;
#endif
}

}  // namespace titan::core
