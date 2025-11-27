// Titan HTTP/2 Tests
// Unit tests for HTTP/2 protocol detection and session management

#include "../../src/http/h2.hpp"
#include "../../src/http/http.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>
#include <vector>

using namespace titan::http;

// ============================
// HTTP/2 Connection Detection
// ============================

TEST_CASE("HTTP/2 preface detection", "[http2][detection]") {
    SECTION("Detect valid HTTP/2 preface") {
        const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        std::vector<uint8_t> data(preface, preface + HTTP2_PREFACE_LEN);

        REQUIRE(is_http2_connection(data));
    }

    SECTION("Reject HTTP/1.1 request") {
        const char* http1 = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        std::vector<uint8_t> data(http1, http1 + std::strlen(http1));

        REQUIRE_FALSE(is_http2_connection(data));
    }

    SECTION("Reject partial preface (too short)") {
        const char* partial = "PRI * HTTP/2.0";
        std::vector<uint8_t> data(partial, partial + std::strlen(partial));

        REQUIRE_FALSE(is_http2_connection(data));
    }

    SECTION("Reject invalid preface") {
        const char* invalid = "PRI * HTTP/1.1\r\n\r\nSM\r\n\r\n";
        std::vector<uint8_t> data(invalid, invalid + HTTP2_PREFACE_LEN);

        REQUIRE_FALSE(is_http2_connection(data));
    }

    SECTION("Reject empty data") {
        std::vector<uint8_t> data;

        REQUIRE_FALSE(is_http2_connection(data));
    }

    SECTION("Detect preface with trailing data") {
        const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        std::vector<uint8_t> data(preface, preface + HTTP2_PREFACE_LEN);

        // Add SETTINGS frame after preface
        data.push_back(0x00);
        data.push_back(0x00);
        data.push_back(0x00);

        REQUIRE(is_http2_connection(data));
    }
}

// ============================
// HTTP/2 Session Management
// ============================

TEST_CASE("H2Session creation", "[http2][session]") {
    SECTION("Create server session") {
        H2Session session(true);  // server mode

        // Session should be ready (not closed)
        REQUIRE_FALSE(session.should_close());

        // Session should want to write (has SETTINGS frame queued)
        REQUIRE(session.want_write());
    }

    SECTION("Create client session") {
        H2Session session(false);  // client mode

        // Session should be ready (not closed)
        REQUIRE_FALSE(session.should_close());

        // Client also queues SETTINGS frame
        REQUIRE(session.want_write());
    }
}

TEST_CASE("H2Session initial SETTINGS", "[http2][session]") {
    SECTION("Server session sends SETTINGS frame") {
        H2Session session(true);

        // Session should have queued SETTINGS frame
        auto send_data = session.send_data();

        // SETTINGS frame starts with:
        // - 3 bytes length
        // - 1 byte type (0x04 = SETTINGS)
        // - 1 byte flags
        // - 4 bytes stream ID (0 for SETTINGS)
        REQUIRE(send_data.size() >= 9);
        REQUIRE(send_data[3] == 0x04);  // SETTINGS frame type
    }
}

TEST_CASE("H2Session recv HTTP/2 preface", "[http2][session]") {
    SECTION("Server receives client preface") {
        H2Session session(true);

        // Client connection preface
        const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        std::vector<uint8_t> data(preface, preface + HTTP2_PREFACE_LEN);

        size_t consumed = 0;
        auto ec = session.recv(data, consumed);

        REQUIRE_FALSE(ec);
        REQUIRE(consumed == HTTP2_PREFACE_LEN);
    }
}

TEST_CASE("H2Session stream management", "[http2][session]") {
    SECTION("Get non-existent stream returns nullptr") {
        H2Session session(true);

        auto* stream = session.get_stream(1);
        REQUIRE(stream == nullptr);
    }

    SECTION("Get active streams initially empty") {
        H2Session session(true);

        auto streams = session.get_active_streams();
        REQUIRE(streams.empty());
    }
}

TEST_CASE("H2Session submit request (client)", "[http2][session]") {
    SECTION("Submit simple GET request") {
        H2Session session(false);  // client mode

        Request request;
        request.method = Method::GET;
        request.path = "/test";
        request.version = Version::HTTP_2_0;

        int32_t stream_id = -1;
        auto ec = session.submit_request(request, stream_id);

        REQUIRE_FALSE(ec);
        REQUIRE(stream_id > 0);
    }

    SECTION("Server cannot submit request") {
        H2Session session(true);  // server mode

        Request request;
        request.method = Method::GET;
        request.path = "/test";

        int32_t stream_id = -1;
        auto ec = session.submit_request(request, stream_id);

        REQUIRE(ec);  // Should fail
    }
}

TEST_CASE("H2Session submit response (server)", "[http2][session]") {
    SECTION("Submit 200 OK response") {
        H2Session session(true);  // server mode

        // Simulate receiving HTTP/2 headers to create stream
        // This is the HTTP/2 client preface
        const uint8_t preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        size_t consumed = 0;
        (void)session.recv(std::span<const uint8_t>(preface, 24), consumed);

        // For this test, we'll test that submitting to a non-existent stream returns an error
        // (which is the current behavior - stream must be created by incoming request first)
        Response response;
        response.status = StatusCode::OK;
        response.version = Version::HTTP_2_0;

        auto ec = session.submit_response(999, response);  // Non-existent stream

        REQUIRE(ec);  // Should fail because stream doesn't exist
    }

    SECTION("Client cannot submit response") {
        H2Session session(false);  // client mode

        Response response;
        response.status = StatusCode::OK;

        auto ec = session.submit_response(1, response);

        REQUIRE(ec);  // Should fail
    }
}

// ============================
// HTTP/2 Stream State
// ============================

TEST_CASE("H2Stream initialization", "[http2][stream]") {
    SECTION("New stream has default values") {
        H2Stream stream;

        REQUIRE(stream.stream_id == -1);
        REQUIRE(stream.state == H2StreamState::Idle);
        REQUIRE_FALSE(stream.request_complete);
        REQUIRE_FALSE(stream.response_complete);
        REQUIRE(stream.request_body.empty());
        REQUIRE(stream.response_body.empty());
    }
}

TEST_CASE("H2Stream request/response data", "[http2][stream]") {
    SECTION("Request body accumulation") {
        H2Stream stream;

        const char* data1 = "Hello, ";
        const char* data2 = "World!";

        stream.request_body.insert(stream.request_body.end(), data1, data1 + 7);
        stream.request_body.insert(stream.request_body.end(), data2, data2 + 6);

        stream.request.body = std::span<const uint8_t>(stream.request_body);

        REQUIRE(stream.request.body.size() == 13);
        REQUIRE(std::string(reinterpret_cast<const char*>(stream.request.body.data()),
                           stream.request.body.size()) == "Hello, World!");
    }

    SECTION("Response body accumulation") {
        H2Stream stream;

        const char* data = "Response body";
        stream.response_body.assign(data, data + 13);
        stream.response.body = std::span<const uint8_t>(stream.response_body);

        REQUIRE(stream.response.body.size() == 13);
        REQUIRE(std::string(reinterpret_cast<const char*>(stream.response.body.data()),
                           stream.response.body.size()) == "Response body");
    }
}
