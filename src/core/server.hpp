// Titan Server - Header
// HTTP server managing connections and request processing

#pragma once

#include "core.hpp"
#include "socket.hpp"
#include "tls.hpp"
#include "../control/config.hpp"
#include "../gateway/pipeline.hpp"
#include "../gateway/router.hpp"
#include "../gateway/upstream.hpp"
#include "../http/parser.hpp"
#include "../http/h2.hpp"

#include <openssl/ssl.h>

#include <netinet/in.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for test access
class ProxyTestFixture;

namespace titan::core {

/// Connection protocol type
enum class Protocol : uint8_t {
    Unknown,   // Not yet determined
    HTTP_1_1,  // HTTP/1.1
    HTTP_2,    // HTTP/2
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

    // TLS state
    SSL* ssl = nullptr;                   // OpenSSL connection object (owned by unique_ptr in Server)
    bool tls_enabled = false;             // Whether this connection uses TLS
    bool tls_handshake_complete = false;  // TLS handshake completion state

    // HTTP/1.1 state
    http::Parser parser;
    http::Request request;
    http::Response response;
    bool keep_alive = true;

    // HTTP/2 state
    std::unique_ptr<http::H2Session> h2_session;
};

/// HTTP server managing connections
class Server {
    // Allow test fixture to access private methods
    friend class ::ProxyTestFixture;

public:
    /// Create server with configuration
    explicit Server(const control::Config& config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Start server (bind and listen)
    [[nodiscard]] std::error_code start();

    /// Stop server
    void stop();

    [[nodiscard]] int listen_fd() const noexcept {
        return listen_fd_;
    }

    /// Process incoming connection
    void handle_accept(int client_fd, std::string_view remote_ip, uint16_t remote_port);

    /// Process data from connection (reads from socket internally)
    void handle_read(int client_fd);

    /// Handle connection close
    void handle_close(int client_fd);

private:
    const control::Config& config_;
    int listen_fd_ = -1;
    bool running_ = false;

    std::unique_ptr<gateway::Router> router_;
    std::unique_ptr<gateway::UpstreamManager> upstream_manager_;
    std::unique_ptr<gateway::Pipeline> pipeline_;

    // TLS support
    std::optional<TlsContext> tls_context_;
    std::unordered_map<int, SslPtr> ssl_connections_;  // fd -> SSL object mapping

    std::unordered_map<int, std::unique_ptr<Connection>> connections_;

    // DNS resolution cache (hostname -> resolved address)
    // Cache is never invalidated for simplicity (MVP)
    // TODO: Add TTL-based expiration for production
    std::unordered_map<std::string, sockaddr_in> dns_cache_;

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

    /// Connect to backend server
    [[nodiscard]] int connect_to_backend(const std::string& host, uint16_t port);

    /// Build HTTP request string to send to backend
    std::string build_backend_request(const http::Request& request);

    /// Receive and parse HTTP response from backend
    bool receive_backend_response(int backend_fd, http::Response& response, std::vector<uint8_t>& body);
};

} // namespace titan::core
