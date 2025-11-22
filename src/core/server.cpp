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

namespace titan::core {

// Backend proxy performance tuning constants
namespace {
    // Buffer sizes optimized for typical API responses
    constexpr size_t kBackendResponseBufferSize = 65536;  // 64KB - most API responses fit
    constexpr size_t kBackendReadChunkSize = 8192;        // 8KB - fewer syscalls
    constexpr size_t kBackendMaxResponseSize = 100 * 1024 * 1024;  // 100MB safety limit

    // Request building size estimates
    constexpr size_t kRequestLineBaseSize = 50;      // "METHOD /path HTTP/1.1\r\n"
    constexpr size_t kRequestHeaderMargin = 50;      // Extra for Connection, Host headers
    constexpr size_t kHeaderSeparatorSize = 4;       // ": \r\n"
    constexpr size_t kQuerySeparatorSize = 1;        // "?"

    // Connection staleness check threshold
    // Only perform expensive MSG_PEEK validation if connection idle > 5s
    constexpr auto kConnectionStaleThreshold = std::chrono::seconds(5);
} // anonymous namespace

Server::Server(const control::Config& config)
    : config_(config)
    , router_(std::make_unique<gateway::Router>())
    , upstream_manager_(std::make_unique<gateway::UpstreamManager>())
    , pipeline_(std::make_unique<gateway::Pipeline>()) {

    // Build router from config
    for (const auto& route_config : config_.routes) {
        gateway::Route route;
        route.path = route_config.path;

        if (!route_config.method.empty()) {
            // Convert method string to enum
            const auto& method_str = route_config.method;
            switch (method_str[0]) {
                case 'G':
                    if (method_str == "GET") route.method = http::Method::GET;
                    break;
                case 'P':
                    if (method_str == "POST") route.method = http::Method::POST;
                    else if (method_str == "PUT") route.method = http::Method::PUT;
                    else if (method_str == "PATCH") route.method = http::Method::PATCH;
                    break;
                case 'D':
                    if (method_str == "DELETE") route.method = http::Method::DELETE;
                    break;
                case 'H':
                    if (method_str == "HEAD") route.method = http::Method::HEAD;
                    break;
                case 'O':
                    if (method_str == "OPTIONS") route.method = http::Method::OPTIONS;
                    break;
            }
        }

        route.handler_id = route_config.handler_id.empty() ? route_config.path : route_config.handler_id;
        route.upstream_name = route_config.upstream;
        route.priority = route_config.priority;

        router_->add_route(std::move(route));
    }

    // Build upstreams from config
    for (const auto& upstream_config : config_.upstreams) {
        auto upstream = std::make_unique<gateway::Upstream>(upstream_config.name);

        for (const auto& backend_config : upstream_config.backends) {
            gateway::Backend backend;
            backend.host = backend_config.host;
            backend.port = backend_config.port;
            backend.weight = backend_config.weight;
            backend.max_connections = backend_config.max_connections;
            upstream->add_backend(std::move(backend));
        }

        upstream_manager_->register_upstream(std::move(upstream));
    }

    // Build middleware pipeline
    // TODO: Add middleware based on config

    // Initialize TLS if enabled
    if (config_.server.tls_enabled) {
        std::error_code error;
        auto result = TlsContext::create(
            config_.server.tls_certificate_path,
            config_.server.tls_private_key_path,
            config_.server.tls_alpn_protocols,
            error
        );

        if (result) {
            tls_context_ = std::move(*result);
        } else {
            // TLS initialization failed - log error
            // TODO: Add proper error handling/logging
            throw std::runtime_error("Failed to initialize TLS context: " + error.message());
        }
    }
}

Server::~Server() {
    stop();
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

    listen_fd_ = create_listening_socket(
        config_.server.listen_address,
        config_.server.listen_port,
        config_.server.backlog
    );

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
        } else if (result == TlsHandshakeResult::WantRead || result == TlsHandshakeResult::WantWrite) {
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
        // TLS read
        n = ssl_read_nonblocking(conn.ssl, buffer);

        if (n <= 0) {
            int err = SSL_get_error(conn.ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // Would block - try again later
                return;
            }
            // Error or EOF - close connection
            handle_close(client_fd);
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
    }

    // Append to buffer
    conn.recv_buffer.insert(conn.recv_buffer.end(), buffer, buffer + n);

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
        auto remaining_data = std::span<const uint8_t>(
            conn.recv_buffer.data() + conn.recv_cursor,
            conn.recv_buffer.size() - conn.recv_cursor
        );

        // Try to parse HTTP/1.1 request
        auto [result, consumed] = conn.parser.parse_request(remaining_data, conn.request);

        if (result == http::ParseResult::Complete) {
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
                conn.recv_buffer.erase(conn.recv_buffer.begin(), conn.recv_buffer.begin() + conn.recv_cursor);
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
    } else if (!match.upstream_name.empty()) {
        // Proxy to backend
        ctx.set_metadata("upstream", std::string(match.upstream_name));

        // Create temporary connection for proxy logic
        // We need conn.request and conn.response_body to be populated
        // The proxy_to_backend will populate conn.response and conn.response_body
        Connection temp_conn;
        temp_conn.request = stream.request;
        temp_conn.remote_ip = conn.remote_ip;
        temp_conn.remote_port = conn.remote_port;

        bool success = proxy_to_backend(temp_conn, ctx);

        if (success) {
            // Move response from temp connection to stream
            stream.response = std::move(temp_conn.response);
            stream.response_body = std::move(temp_conn.response_body);
            // Fix span to point to new location (span is invalidated by move)
            stream.response.body = stream.response_body;
        } else {
            // Proxying failed - return 502
            stream.response.status = http::StatusCode::BadGateway;
        }
    } else {
        // Direct response
        stream.response.status = http::StatusCode::OK;
    }

    // Submit response to HTTP/2 session
    stream.response_complete = true;
    auto ec = conn.h2_session->submit_response(stream.stream_id, stream.response);
    if (ec) {
        // Failed to submit response - log error (TODO: proper error handling)
    }
}

void Server::handle_close(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return;
    }

    // Clean up SSL connection if exists
    ssl_connections_.erase(client_fd);

    close_fd(it->second->fd);
    connections_.erase(it);
}

bool Server::process_request(Connection& conn) {
    // Match route
    auto match = router_->match(conn.request.method, conn.request.path);

    // Build request context
    gateway::RequestContext ctx;
    ctx.request = &conn.request;
    ctx.response = &conn.response;
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
        client_wants_keepalive = false; // HTTP/1.0 defaults to close
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

    if (match.handler_id.empty()) {
        // No route matched - 404
        send_response(conn, client_wants_keepalive);
        return client_wants_keepalive;
    }

    // Proxy to backend if upstream is configured
    if (!match.upstream_name.empty()) {
        ctx.set_metadata("upstream", std::string(match.upstream_name));

        bool success = proxy_to_backend(conn, ctx);
        if (!success) {
            // Proxying failed, return 502 Bad Gateway
            conn.response.status = http::StatusCode::BadGateway;
        }

        send_response(conn, client_wants_keepalive);
        return client_wants_keepalive;
    }

    // No upstream configured - return stub response
    conn.response.status = http::StatusCode::OK;
    send_response(conn, client_wants_keepalive);
    return client_wants_keepalive;
}

void Server::send_response(Connection& conn, bool keep_alive) {
    // Build response string
    std::string response_str;

    // Pre-reserve capacity to avoid allocations (estimate: 200 bytes headers + body size)
    size_t body_size = conn.response.body.empty() ? 0 : conn.response.body.size();
    size_t estimated_size = 200 + body_size;
    for (const auto& header : conn.response.headers) {
        estimated_size += header.name.size() + header.value.size() + 4; // ": \r\n"
    }
    response_str.reserve(estimated_size);

    // Status line with reason phrase
    response_str += "HTTP/1.1 ";
    response_str += std::to_string(static_cast<int>(conn.response.status));
    response_str += " ";
    response_str += http::to_reason_phrase(conn.response.status);
    response_str += "\r\n";

    // Forward headers from backend response (except Content-Length and Connection)
    for (const auto& header : conn.response.headers) {
        // Skip headers we'll add ourselves
        if (header.name == "Content-Length" || header.name == "content-length" ||
            header.name == "Connection" || header.name == "connection") {
            continue;
        }
        response_str += header.name;
        response_str += ": ";
        response_str += header.value;
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
        response_str.append(
            reinterpret_cast<const char*>(conn.response.body.data()),
            conn.response.body.size());
    }

    // Send (use TLS if enabled)
    if (conn.tls_enabled) {
        (void)ssl_write_nonblocking(conn.ssl, std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(response_str.data()),
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
    // Get upstream name from context
    auto upstream_name = ctx.get_metadata("upstream");
    if (upstream_name.empty()) {
        return false;
    }

    // Get upstream from manager
    auto* upstream = upstream_manager_->get_upstream(upstream_name);
    if (!upstream) {
        return false;
    }

    // Get connection from pool (might be cached!)
    auto* backend_conn = upstream->get_connection(conn.remote_ip);
    if (!backend_conn || !backend_conn->backend) {
        return false;
    }

    // Check if connection is valid and reusable
    if (!backend_conn->is_valid()) {
        // Need to establish new connection
        backend_conn->sockfd = connect_to_backend(
            backend_conn->backend->host,
            backend_conn->backend->port);

        if (backend_conn->sockfd < 0) {
            upstream->release_connection(backend_conn);
            return false;
        }

        backend_conn->created_at = std::chrono::steady_clock::now();
    } else {
        // Connection exists - only validate if it's been idle for a while
        // Frequent reuse doesn't need validation (reduces syscall overhead)
        auto now = std::chrono::steady_clock::now();
        auto idle_duration = now - backend_conn->last_used;

        if (idle_duration > kConnectionStaleThreshold) {
            // Connection might be stale - use MSG_PEEK to check if still alive
            char peek_buf[1];
            ssize_t peek_result = recv(backend_conn->sockfd, peek_buf, 1, MSG_PEEK | MSG_DONTWAIT);

            // If peek returns 0, connection was closed by peer
            // If peek returns -1 with EAGAIN/EWOULDBLOCK, connection is alive but no data
            // If peek returns -1 with other errors, connection is broken
            if (peek_result == 0 || (peek_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                // Connection is dead, close it and reconnect
                close_fd(backend_conn->sockfd);
                backend_conn->sockfd = connect_to_backend(
                    backend_conn->backend->host,
                    backend_conn->backend->port);

                if (backend_conn->sockfd < 0) {
                    upstream->release_connection(backend_conn);
                    return false;
                }

                backend_conn->created_at = std::chrono::steady_clock::now();
            }
        }
        // If connection was recently used, trust it's still valid
        // send() will fail if it's not, and we'll reconnect then
    }

    // Build HTTP request to send to backend
    std::string request_str = build_backend_request(conn.request);

    // Send request to backend
    ssize_t sent = send(backend_conn->sockfd, request_str.data(), request_str.size(), 0);
    if (sent < 0 || static_cast<size_t>(sent) != request_str.size()) {
        // Send failed - close connection and release
        close_fd(backend_conn->sockfd);
        backend_conn->sockfd = -1;
        upstream->release_connection(backend_conn);
        return false;
    }

    // Receive and parse response from backend
    bool success = receive_backend_response(backend_conn->sockfd, conn.response, conn.response_body);

    // Update connection state
    backend_conn->last_used = std::chrono::steady_clock::now();
    backend_conn->requests_served++;

    if (!success) {
        // Response parsing failed - close connection
        close_fd(backend_conn->sockfd);
        backend_conn->sockfd = -1;
        upstream->release_connection(backend_conn);
        return false;
    }

    // Note: response.body already points into conn.response_body (zero-copy)
    // The parser sets this correctly to point to just the body portion
    // We must NOT overwrite it here!

    // Release connection back to pool for reuse
    upstream->release_connection(backend_conn);

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
                close(sockfd);
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
        close(sockfd);
        return -1;
    }

    // Enable TCP_NODELAY to reduce latency (disable Nagle's algorithm)
    // This is critical for API gateway workloads with small messages
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    return sockfd;
}

std::string Server::build_backend_request(const http::Request& request) {
    std::string req;

    // Calculate size to avoid reallocation
    // Format: METHOD path[?query] HTTP/1.1\r\n + headers + \r\n + body
    size_t estimated_size = kRequestLineBaseSize;
    estimated_size += request.path.size();
    estimated_size += request.query.empty() ? 0 : (kQuerySeparatorSize + request.query.size());

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
    req += request.path;
    if (!request.query.empty()) {
        req += "?";
        req += request.query;
    }
    req += " HTTP/1.1\r\n";

    // Forward headers (except Connection header - we'll set our own)
    bool has_host = false;
    for (const auto& header : request.headers) {
        // Skip Connection header - we want keep-alive for backend pooling
        if (header.name == "Connection" || header.name == "connection") {
            continue;
        }

        if (header.name == "Host" || header.name == "host") {
            has_host = true;
        }

        req += header.name;
        req += ": ";
        req += header.value;
        req += "\r\n";
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

bool Server::receive_backend_response(int backend_fd, http::Response& response, std::vector<uint8_t>& buffer) {
    buffer.clear();

    // Pre-reserve larger buffer to avoid multiple reallocations
    buffer.reserve(kBackendResponseBufferSize);

    http::Parser parser;
    uint8_t chunk[kBackendReadChunkSize];

    // Phase 1: Read with blocking until we get some data
    ssize_t n = recv(backend_fd, chunk, sizeof(chunk), 0);
    if (n <= 0) {
        return false;  // Connection error or closed
    }

    // Append to buffer (uses memcpy internally, faster than insert with iterators)
    buffer.insert(buffer.end(), chunk, chunk + n);

    // Phase 2: Continue reading all immediately available data (non-blocking)
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

    // Phase 3: Parse the response
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

    // Phase 4: If still incomplete, keep reading until complete or timeout
    while (result == http::ParseResult::Incomplete) {
        n = recv(backend_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;  // Timeout or connection closed
        }

        buffer.insert(buffer.end(), chunk, chunk + n);

        // Reset parser and re-parse the entire buffer from scratch
        // Parser maintains state, so we must reset before re-parsing accumulated data
        parser.reset();
        auto [new_result, new_consumed] = parser.parse_response(
            std::span<const uint8_t>(buffer), response);
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

} // namespace titan::core
