// Titan Proxy Functions Unit Tests
// Tests for backend connection, request building, and response parsing

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <thread>

#include "../../src/core/server.hpp"
#include "../../src/gateway/factory.hpp"
#include "../../src/http/http.hpp"

using namespace titan;
using namespace titan::core;
using namespace titan::http;

// Test fixture with access to private methods via friend declaration
class ProxyTestFixture {
public:
    ProxyTestFixture()
        : config_(create_test_config()),
          router_(gateway::build_router(config_)),
          upstream_manager_(gateway::build_upstream_manager(config_)),
          pipeline_(gateway::build_pipeline(config_, upstream_manager_.get(), nullptr, nullptr)),
          server_(config_, std::move(router_), std::move(upstream_manager_), std::move(pipeline_)) {
    }

    // Wrapper methods to access private Server methods
    std::string test_build_backend_request(const Request& req) {
        std::unordered_map<std::string, std::string> empty_metadata;
        return server_.build_backend_request(req, empty_metadata);
    }

    bool test_receive_backend_response(int fd, Response& resp, std::vector<uint8_t>& body) {
        return server_.receive_backend_response(fd, resp, body);
    }

    int test_connect_to_backend(const std::string& host, uint16_t port) {
        return server_.connect_to_backend(host, port);
    }

private:
    control::Config create_test_config() {
        control::Config cfg;
        cfg.server.listen_address = "127.0.0.1";
        cfg.server.listen_port = 8080;
        cfg.server.backlog = 128;
        cfg.server.worker_threads = 1;
        return cfg;
    }

    control::Config config_;
    std::unique_ptr<gateway::Router> router_;
    std::unique_ptr<gateway::UpstreamManager> upstream_manager_;
    std::unique_ptr<gateway::Pipeline> pipeline_;
    Server server_;
};

// ============================================================================
// build_backend_request() Tests
// ============================================================================

TEST_CASE("build_backend_request - Simple GET request", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    Request req;
    req.method = Method::GET;
    req.path = "/api/users";
    req.version = Version::HTTP_1_1;
    req.headers.push_back({"Host", "backend.example.com"});
    req.headers.push_back({"User-Agent", "Titan/1.0"});

    std::string result = fixture.test_build_backend_request(req);

    REQUIRE(result.find("GET /api/users HTTP/1.1\r\n") == 0);
    REQUIRE(result.find("Host: backend.example.com\r\n") != std::string::npos);
    REQUIRE(result.find("User-Agent: Titan/1.0\r\n") != std::string::npos);
    REQUIRE(result.find("\r\n\r\n") != std::string::npos);
}

TEST_CASE("build_backend_request - GET with query string", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    Request req;
    req.method = Method::GET;
    req.path = "/api/users";
    req.query = "id=123&name=test";
    req.version = Version::HTTP_1_1;
    req.headers.push_back({"Host", "backend.example.com"});

    std::string result = fixture.test_build_backend_request(req);

    REQUIRE(result.find("GET /api/users?id=123&name=test HTTP/1.1\r\n") == 0);
    REQUIRE(result.find("Host: backend.example.com\r\n") != std::string::npos);
}

TEST_CASE("build_backend_request - POST with body", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    const char* body_str = "{\"name\":\"test\",\"value\":42}";
    std::vector<uint8_t> body_data(body_str, body_str + std::strlen(body_str));

    Request req;
    req.method = Method::POST;
    req.path = "/api/data";
    req.version = Version::HTTP_1_1;
    req.headers.push_back({"Host", "backend.example.com"});
    req.headers.push_back({"Content-Type", "application/json"});
    req.headers.push_back({"Content-Length", "27"});
    req.body = std::span<const uint8_t>(body_data);

    std::string result = fixture.test_build_backend_request(req);

    REQUIRE(result.find("POST /api/data HTTP/1.1\r\n") == 0);
    REQUIRE(result.find("Content-Type: application/json\r\n") != std::string::npos);
    REQUIRE(result.find("Content-Length: 27\r\n") != std::string::npos);
    REQUIRE(result.find("\r\n\r\n") != std::string::npos);
    REQUIRE(result.find("{\"name\":\"test\",\"value\":42}") != std::string::npos);
}

TEST_CASE("build_backend_request - Injects Host header when missing", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    Request req;
    req.method = Method::GET;
    req.path = "/test";
    req.version = Version::HTTP_1_1;
    req.headers.push_back({"User-Agent", "Titan/1.0"});
    // No Host header

    std::string result = fixture.test_build_backend_request(req);

    REQUIRE(result.find("GET /test HTTP/1.1\r\n") == 0);
    REQUIRE(result.find("Host: backend\r\n") != std::string::npos);
}

TEST_CASE("build_backend_request - Preserves existing Host header", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    Request req;
    req.method = Method::GET;
    req.path = "/test";
    req.version = Version::HTTP_1_1;
    req.headers.push_back({"Host", "mybackend.com"});

    std::string result = fixture.test_build_backend_request(req);

    REQUIRE(result.find("Host: mybackend.com\r\n") != std::string::npos);
    // Should not have duplicate Host header
    size_t first_host = result.find("Host:");
    size_t second_host = result.find("Host:", first_host + 1);
    REQUIRE(second_host == std::string::npos);
}

TEST_CASE("build_backend_request - Multiple HTTP methods", "[proxy][build_request]") {
    ProxyTestFixture fixture;

    SECTION("PUT") {
        Request req;
        req.method = Method::PUT;
        req.path = "/resource/123";
        req.headers.push_back({"Host", "backend"});

        std::string result = fixture.test_build_backend_request(req);
        REQUIRE(result.find("PUT /resource/123 HTTP/1.1\r\n") == 0);
    }

    SECTION("DELETE") {
        Request req;
        req.method = Method::DELETE;
        req.path = "/resource/456";
        req.headers.push_back({"Host", "backend"});

        std::string result = fixture.test_build_backend_request(req);
        REQUIRE(result.find("DELETE /resource/456 HTTP/1.1\r\n") == 0);
    }

    SECTION("PATCH") {
        Request req;
        req.method = Method::PATCH;
        req.path = "/resource/789";
        req.headers.push_back({"Host", "backend"});

        std::string result = fixture.test_build_backend_request(req);
        REQUIRE(result.find("PATCH /resource/789 HTTP/1.1\r\n") == 0);
    }
}

// ============================================================================
// receive_backend_response() Tests
// ============================================================================

// Helper: Create a socketpair and write response to one end
class MockBackend {
public:
    MockBackend() {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_) != 0) {
            throw std::runtime_error("Failed to create socketpair");
        }
    }

    ~MockBackend() {
        close(sockets_[0]);
        close(sockets_[1]);
    }

    // Get the socket fd for Server to read from
    int get_client_fd() const { return sockets_[0]; }

    // Write response data to the backend side
    void write_response(const std::string& data) { send(sockets_[1], data.data(), data.size(), 0); }

    // Write response in chunks with delays (simulate slow backend)
    void write_response_chunked(const std::vector<std::string>& chunks, int delay_ms = 10) {
        for (const auto& chunk : chunks) {
            send(sockets_[1], chunk.data(), chunk.size(), 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    void close_backend() {
        close(sockets_[1]);
        sockets_[1] = -1;
    }

private:
    int sockets_[2];
};

TEST_CASE("receive_backend_response - Simple 200 OK", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "Hello";

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::OK);
    REQUIRE(resp.version == Version::HTTP_1_1);
    REQUIRE(resp.get_header("Content-Type") == "text/plain");
    REQUIRE(resp.get_header("Content-Length") == "5");

    // Body should be a span pointing to just the body portion
    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == "Hello");
}

TEST_CASE("receive_backend_response - JSON response", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string json_body = "{\"status\":\"success\",\"data\":{\"id\":123}}";
    std::string response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        std::to_string(json_body.size()) +
        "\r\n"
        "\r\n" +
        json_body;

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::OK);
    REQUIRE(resp.get_header("Content-Type") == "application/json");

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == json_body);
}

TEST_CASE("receive_backend_response - 404 Not Found", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string response_str =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 9\r\n"
        "\r\n"
        "Not Found";

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::NotFound);

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == "Not Found");
}

TEST_CASE("receive_backend_response - 500 Internal Server Error", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string response_str =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 14\r\n"
        "\r\n"
        "Server Error!!";

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::InternalServerError);

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == "Server Error!!");
}

TEST_CASE("receive_backend_response - Empty body (204 No Content)", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string response_str =
        "HTTP/1.1 204 No Content\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::NoContent);
    REQUIRE(resp.body.empty());
}

TEST_CASE("receive_backend_response - Large response (multi-read)", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    // Create a 10KB response
    std::string large_body(10 * 1024, 'X');
    std::string response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " +
        std::to_string(large_body.size()) +
        "\r\n"
        "\r\n" +
        large_body;

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::OK);
    REQUIRE(resp.body.size() == 10 * 1024);
    REQUIRE(resp.body[0] == 'X');
    REQUIRE(resp.body[resp.body.size() - 1] == 'X');
}

TEST_CASE("receive_backend_response - Response split across multiple recv()",
          "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    // Write response in chunks on separate thread
    std::thread writer([&backend]() {
        backend.write_response("HTTP/1.1 200 OK\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        backend.write_response("Content-Type: text/plain\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        backend.write_response("Content-Length: 11\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        backend.write_response("\r\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        backend.write_response("Hello World");
    });

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    writer.join();

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::OK);

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == "Hello World");
}

TEST_CASE("receive_backend_response - Connection closed prematurely", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    // Write incomplete response then close
    backend.write_response("HTTP/1.1 200 OK\r\n");
    backend.close_backend();

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    // Should fail because response is incomplete
    REQUIRE_FALSE(success);
}

TEST_CASE("receive_backend_response - Multiple headers", "[proxy][receive_response]") {
    ProxyTestFixture fixture;
    MockBackend backend;

    std::string response_str =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 2\r\n"
        "X-Custom-Header: custom-value\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "{}";

    backend.write_response(response_str);

    Response resp;
    std::vector<uint8_t> body;
    bool success = fixture.test_receive_backend_response(backend.get_client_fd(), resp, body);

    REQUIRE(success);
    REQUIRE(resp.status == StatusCode::OK);
    REQUIRE(resp.get_header("Content-Type") == "application/json");
    REQUIRE(resp.get_header("X-Custom-Header") == "custom-value");
    REQUIRE(resp.get_header("Cache-Control") == "no-cache");
    REQUIRE(resp.get_header("Connection") == "keep-alive");

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
    REQUIRE(body_str == "{}");
}

// ============================================================================
// connect_to_backend() Tests
// ============================================================================

TEST_CASE("connect_to_backend - Connect to localhost by IP", "[proxy][connect]") {
    // Start a simple listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listen_sock >= 0);

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);  // Let OS assign port

    REQUIRE(bind(listen_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(listen_sock, 1) == 0);

    // Get assigned port
    socklen_t addr_len = sizeof(addr);
    REQUIRE(getsockname(listen_sock, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) == 0);
    uint16_t port = ntohs(addr.sin_port);

    // Test connection
    ProxyTestFixture fixture;
    int client_fd = fixture.test_connect_to_backend("127.0.0.1", port);

    REQUIRE(client_fd >= 0);

    // Accept connection on server side to verify
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int accepted =
        accept(listen_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    REQUIRE(accepted >= 0);

    close(client_fd);
    close(accepted);
    close(listen_sock);
}

TEST_CASE("connect_to_backend - Connect to localhost by hostname", "[proxy][connect]") {
    // Start a simple listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listen_sock >= 0);

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    REQUIRE(bind(listen_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(listen_sock, 1) == 0);

    socklen_t addr_len = sizeof(addr);
    REQUIRE(getsockname(listen_sock, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) == 0);
    uint16_t port = ntohs(addr.sin_port);

    // Test connection using hostname
    ProxyTestFixture fixture;
    int client_fd = fixture.test_connect_to_backend("localhost", port);

    REQUIRE(client_fd >= 0);

    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int accepted =
        accept(listen_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    REQUIRE(accepted >= 0);

    close(client_fd);
    close(accepted);
    close(listen_sock);
}

TEST_CASE("connect_to_backend - Connection refused", "[proxy][connect]") {
    ProxyTestFixture fixture;

    // Try to connect to a port that's not listening
    int client_fd =
        fixture.test_connect_to_backend("127.0.0.1", 1);  // Port 1 (typically privileged)

    // Should fail (return -1)
    REQUIRE(client_fd == -1);
}

TEST_CASE("connect_to_backend - Invalid hostname", "[proxy][connect]") {
    ProxyTestFixture fixture;

    int client_fd = fixture.test_connect_to_backend("this-host-does-not-exist-12345.invalid", 80);

    REQUIRE(client_fd == -1);
}
