// Titan HTTP Layer Unit Tests

#include <catch2/catch_test_macros.hpp>

#include "../../src/http/http.hpp"
#include "../../src/http/parser.hpp"

using namespace titan::http;

TEST_CASE("HTTP method conversion", "[http][method]") {
    REQUIRE(to_string(Method::GET) == "GET");
    REQUIRE(to_string(Method::POST) == "POST");
    REQUIRE(to_string(Method::PUT) == "PUT");
    REQUIRE(to_string(Method::DELETE) == "DELETE");

    REQUIRE(parse_method("GET") == Method::GET);
    REQUIRE(parse_method("POST") == Method::POST);
    REQUIRE(parse_method("UNKNOWN") == Method::UNKNOWN);
}

TEST_CASE("HTTP version conversion", "[http][version]") {
    REQUIRE(to_string(Version::HTTP_1_0) == "HTTP/1.0");
    REQUIRE(to_string(Version::HTTP_1_1) == "HTTP/1.1");
    REQUIRE(to_string(Version::HTTP_2_0) == "HTTP/2.0");
}

TEST_CASE("Header name comparison (case-insensitive)", "[http][headers]") {
    REQUIRE(header_name_equals("Content-Type", "content-type"));
    REQUIRE(header_name_equals("CONTENT-TYPE", "content-type"));
    REQUIRE(header_name_equals("content-type", "Content-Type"));
    REQUIRE_FALSE(header_name_equals("Content-Type", "Content-Length"));
}

TEST_CASE("Parse simple GET request", "[http][parser]") {
    const char* raw_request =
        "GET /hello HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: test\r\n"
        "\r\n";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);
    REQUIRE(consumed == data.size());
    REQUIRE(request.method == Method::GET);
    REQUIRE(request.version == Version::HTTP_1_1);
    REQUIRE(request.path == "/hello");
    REQUIRE(request.headers.size() == 2);
    REQUIRE(request.get_header("Host") == "example.com");
    REQUIRE(request.get_header("User-Agent") == "test");
}

TEST_CASE("Parse GET request with query string", "[http][parser]") {
    const char* raw_request =
        "GET /api/users?id=123&name=test HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);
    REQUIRE(request.method == Method::GET);
    REQUIRE(request.path == "/api/users");
    REQUIRE(request.query == "id=123&name=test");
    REQUIRE(request.uri == "/api/users?id=123&name=test");
}

TEST_CASE("Parse POST request with body", "[http][parser]") {
    const char* raw_request =
        "POST /api/data HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"test\":true}";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);
    REQUIRE(request.method == Method::POST);
    REQUIRE(request.path == "/api/data");
    REQUIRE(request.get_header("Content-Type") == "application/json");
    REQUIRE(request.content_length() == 13);
    REQUIRE(request.body.size() == 13);

    std::string body_str(reinterpret_cast<const char*>(request.body.data()), request.body.size());
    REQUIRE(body_str == "{\"test\":true}");
}

TEST_CASE("Parse incomplete request", "[http][parser]") {
    const char* raw_request =
        "GET /hello HTTP/1.1\r\n"
        "Host: example.com\r\n";
    // Missing final \r\n

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Incomplete);
}

TEST_CASE("Request header helpers", "[http][request]") {
    Request request;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"Content-Length", "42"});
    request.headers.push_back({"Host", "example.com"});

    SECTION("Find header (case-insensitive)") {
        REQUIRE(request.has_header("Content-Type"));
        REQUIRE(request.has_header("content-type"));
        REQUIRE(request.has_header("CONTENT-TYPE"));
        REQUIRE_FALSE(request.has_header("User-Agent"));
    }

    SECTION("Get header value") {
        REQUIRE(request.get_header("Host") == "example.com");
        REQUIRE(request.get_header("content-type") == "application/json");
        REQUIRE(request.get_header("Missing", "default") == "default");
    }

    SECTION("Content-Length parsing") {
        REQUIRE(request.content_length() == 42);
    }
}

TEST_CASE("Request keep-alive detection", "[http][request]") {
    Request request;

    SECTION("HTTP/1.1 defaults to keep-alive") {
        request.version = Version::HTTP_1_1;
        REQUIRE(request.keep_alive());
    }

    SECTION("HTTP/1.1 with Connection: close") {
        request.version = Version::HTTP_1_1;
        request.headers.push_back({"Connection", "close"});
        REQUIRE_FALSE(request.keep_alive());
    }

    SECTION("HTTP/1.0 defaults to close") {
        request.version = Version::HTTP_1_0;
        REQUIRE_FALSE(request.keep_alive());
    }

    SECTION("HTTP/1.0 with Connection: keep-alive") {
        request.version = Version::HTTP_1_0;
        request.headers.push_back({"Connection", "keep-alive"});
        REQUIRE(request.keep_alive());
    }
}

TEST_CASE("Parse multiple methods", "[http][parser]") {
    const std::vector<std::pair<const char*, Method>> test_cases = {
        {"GET /test HTTP/1.1\r\n\r\n", Method::GET},
        {"POST /test HTTP/1.1\r\n\r\n", Method::POST},
        {"PUT /test HTTP/1.1\r\n\r\n", Method::PUT},
        {"DELETE /test HTTP/1.1\r\n\r\n", Method::DELETE},
        {"HEAD /test HTTP/1.1\r\n\r\n", Method::HEAD},
        {"OPTIONS /test HTTP/1.1\r\n\r\n", Method::OPTIONS},
        {"PATCH /test HTTP/1.1\r\n\r\n", Method::PATCH},
    };

    for (const auto& [raw_request, expected_method] : test_cases) {
        auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                             std::strlen(raw_request));

        Parser parser;
        Request request;
        auto [result, consumed] = parser.parse_request(data, request);

        REQUIRE(result == ParseResult::Complete);
        REQUIRE(request.method == expected_method);
    }
}

TEST_CASE("Parser reset", "[http][parser]") {
    Parser parser;
    Request request1;

    const char* raw1 = "GET /test1 HTTP/1.1\r\n\r\n";
    auto data1 =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw1), std::strlen(raw1));

    auto [result1, consumed1] = parser.parse_request(data1, request1);
    REQUIRE(result1 == ParseResult::Complete);
    REQUIRE(request1.path == "/test1");

    // Reset parser for next request
    parser.reset();

    Request request2;
    const char* raw2 = "POST /test2 HTTP/1.1\r\n\r\n";
    auto data2 =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw2), std::strlen(raw2));

    auto [result2, consumed2] = parser.parse_request(data2, request2);
    REQUIRE(result2 == ParseResult::Complete);
    REQUIRE(request2.path == "/test2");
    REQUIRE(request2.method == Method::POST);
}

TEST_CASE("Convenience wrapper parse_http_request", "[http][parser]") {
    const char* raw_request = "GET /hello HTTP/1.1\r\n\r\n";
    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    auto maybe_request = parse_http_request(data);
    REQUIRE(maybe_request.has_value());
    REQUIRE(maybe_request->method == Method::GET);
    REQUIRE(maybe_request->path == "/hello");
}

// ============================================================================
// HTTP Keep-Alive Tests
// ============================================================================

TEST_CASE("HTTP/1.1 defaults to keep-alive", "[http][keepalive]") {
    const char* raw_request =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);
    REQUIRE(request.version == Version::HTTP_1_1);

    // HTTP/1.1 should default to keep-alive (no explicit header needed)
    auto conn_header = request.get_header("Connection");
    // Either no header (defaults to keep-alive) or explicitly says keep-alive
    REQUIRE((conn_header.empty() || conn_header.find("keep-alive") != std::string_view::npos));
}

TEST_CASE("HTTP/1.0 with Connection: keep-alive", "[http][keepalive]") {
    const char* raw_request =
        "GET / HTTP/1.0\r\n"
        "Host: example.com\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);
    REQUIRE(request.version == Version::HTTP_1_0);

    auto conn_header = request.get_header("Connection");
    REQUIRE(conn_header == "keep-alive");
}

TEST_CASE("Connection: close overrides HTTP/1.1 default", "[http][keepalive]") {
    const char* raw_request =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(raw_request),
                                         std::strlen(raw_request));

    Parser parser;
    Request request;
    auto [result, consumed] = parser.parse_request(data, request);

    REQUIRE(result == ParseResult::Complete);

    auto conn_header = request.get_header("Connection");
    REQUIRE(conn_header == "close");
}

// ============================================================================
// HTTP Pipelining Tests
// ============================================================================
// Note: Pipelining is handled at the application level (Server::handle_http1)
// llhttp by design consumes the entire buffer (greedy parsing)

TEST_CASE("Parser reset clears state between pipelined requests", "[http][pipelining]") {
    const char* request1_raw = "GET /first HTTP/1.1\r\nHost: example.com\r\n\r\n";
    const char* request2_raw = "POST /second HTTP/1.1\r\nHost: example.com\r\n\r\n";

    Parser parser;

    // Parse first request
    auto data1 = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(request1_raw),
                                          std::strlen(request1_raw));

    Request req1;
    auto [result1, _1] = parser.parse_request(data1, req1);
    REQUIRE(result1 == ParseResult::Complete);
    REQUIRE(req1.method == Method::GET);

    // Reset and parse second request - should not have state from first
    parser.reset();

    auto data2 = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(request2_raw),
                                          std::strlen(request2_raw));

    Request req2;
    auto [result2, _2] = parser.parse_request(data2, req2);
    REQUIRE(result2 == ParseResult::Complete);
    REQUIRE(req2.method == Method::POST);
    REQUIRE(req2.path == "/second");
}

// ============================================================================
// Response Header Tests - Heap Safety
// ============================================================================

TEST_CASE("Response - add_header handles temporary strings without use-after-free",
          "[http][response][memory-safety]") {
    Response resp;

    SECTION("Single temporary string (JWT middleware scenario)") {
        // Simulate middleware passing temporary string (the bug scenario)
        // Before the fix, this would cause heap-use-after-free
        std::string scheme = "Bearer";
        resp.add_header("WWW-Authenticate", scheme + " realm=\"titan\"");

        // Verify header value is still valid (not dangling)
        auto* header = resp.find_header("WWW-Authenticate");
        REQUIRE(header != nullptr);
        REQUIRE(header->value == "Bearer realm=\"titan\"");
    }

    SECTION("Multiple temporary strings with deque growth") {
        // Add many headers with temporary VALUES to trigger deque growth
        // Header names are literals (as in real usage), but values are temporaries
        // This ensures deque doesn't invalidate references like vector would
        for (int i = 0; i < 100; i++) {
            std::string value = "value-" + std::to_string(i);
            // Note: In real usage, header names are always string literals
            // Only values may be temporary strings (e.g., from concatenation)
            resp.add_header("X-Custom", value);
        }

        // Verify the LAST header value is still valid
        // All 100 headers have the same name, so find_header returns the first one
        auto* header = resp.find_header("X-Custom");
        REQUIRE(header != nullptr);
        // Should have the first value added
        REQUIRE(header->value == "value-0");

        // Verify we added all 100 headers
        REQUIRE(resp.headers.size() == 100);
    }

    SECTION("Mixed temporary and literal strings") {
        // Add header with temporary VALUE (the bug scenario)
        std::string auth_scheme = "Bearer";
        resp.add_header("Authorization", auth_scheme + " token123");

        // Add header with string literal value
        resp.add_header("Content-Type", "application/json");

        // Add more headers with temporary values to trigger deque growth
        for (int i = 0; i < 20; i++) {
            std::string value = "val-" + std::to_string(i);
            resp.add_header("X-Test", value);
        }

        // Verify first header is still valid (not invalidated by deque growth)
        auto* auth_header = resp.find_header("Authorization");
        REQUIRE(auth_header != nullptr);
        REQUIRE(auth_header->value == "Bearer token123");

        // Verify literal string header
        auto* content_type = resp.find_header("Content-Type");
        REQUIRE(content_type != nullptr);
        REQUIRE(content_type->value == "application/json");
    }
}

TEST_CASE("Response - owned_header_values uses deque for reference stability",
          "[http][response][memory-safety]") {
    Response resp;

    // Add first header
    std::string value1 = "first-value";
    resp.add_header("X-First", value1);

    // Get pointer to first header's value
    auto* first_header = resp.find_header("X-First");
    REQUIRE(first_header != nullptr);
    const char* first_value_ptr = first_header->value.data();

    // Add many more headers to force deque to allocate new chunks
    // If we used vector, this would invalidate first_value_ptr
    for (int i = 0; i < 100; i++) {
        resp.add_header("X-Header-" + std::to_string(i), "value-" + std::to_string(i));
    }

    // Verify first header's value pointer is STILL VALID
    // This proves deque doesn't invalidate references
    first_header = resp.find_header("X-First");
    REQUIRE(first_header != nullptr);
    REQUIRE(first_header->value == "first-value");
    REQUIRE(first_header->value.data() == first_value_ptr);  // Same pointer!
}
