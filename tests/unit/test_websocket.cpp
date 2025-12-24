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

// Titan WebSocket Tests
// Comprehensive unit tests for WebSocket protocol support

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../../src/http/http.hpp"
#include "../../src/http/websocket.hpp"

using namespace titan::http;

// ========================================
// WebSocket Handshake Tests
// ========================================

TEST_CASE("WebSocket handshake - Valid upgrade request", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {{"Upgrade", "websocket"},
                   {"Connection", "Upgrade"},
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == true);
}

TEST_CASE("WebSocket handshake - Sec-WebSocket-Accept computation (RFC 6455 test vector)",
          "[websocket][handshake]") {
    // RFC 6455 §4.2.2 provides a test vector:
    // Key: "dGhlIHNhbXBsZSBub25jZQ=="
    // Expected Accept: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string accept = WebSocketUtils::compute_accept_key(key);

    REQUIRE(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("WebSocket handshake - Missing Upgrade header", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {{"Connection", "Upgrade"},
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Wrong Upgrade value", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {{"Upgrade", "http2"},  // Wrong value
                   {"Connection", "Upgrade"},
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Missing Sec-WebSocket-Key", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {
        {"Upgrade", "websocket"}, {"Connection", "Upgrade"}, {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Wrong WebSocket version", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {
        {"Upgrade", "websocket"},
        {"Connection", "Upgrade"},
        {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
        {"Sec-WebSocket-Version", "12"}  // Only version 13 is supported
    };

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Non-GET method", "[websocket][handshake]") {
    Request req;
    req.method = Method::POST;  // Must be GET
    req.headers = {{"Upgrade", "websocket"},
                   {"Connection", "Upgrade"},
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Missing Connection Upgrade", "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {{"Upgrade", "websocket"},
                   {"Connection", "keep-alive"},  // Missing "Upgrade"
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == false);
}

TEST_CASE("WebSocket handshake - Connection with multiple values including Upgrade",
          "[websocket][handshake]") {
    Request req;
    req.method = Method::GET;
    req.headers = {{"Upgrade", "websocket"},
                   {"Connection", "keep-alive, Upgrade"},  // Multiple values
                   {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
                   {"Sec-WebSocket-Version", "13"}};

    REQUIRE(WebSocketUtils::is_valid_upgrade_request(req) == true);
}

TEST_CASE("WebSocket upgrade response - 101 Switching Protocols", "[websocket][handshake]") {
    std::string accept_key = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    std::string response = WebSocketUtils::create_upgrade_response(accept_key);

    REQUIRE(response.find("HTTP/1.1 101 Switching Protocols") != std::string::npos);
    REQUIRE(response.find("Upgrade: websocket") != std::string::npos);
    REQUIRE(response.find("Connection: Upgrade") != std::string::npos);
    REQUIRE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
            std::string::npos);
}

// ========================================
// WebSocket Frame Parser Tests
// ========================================

TEST_CASE("WebSocket frame parser - Simple text frame (unmasked)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Text frame: FIN=1, opcode=1 (text), MASK=0, len=5, payload="Hello"
    std::vector<uint8_t> data = {0x81, 0x05,  // FIN=1, opcode=1, MASK=0, len=5
                                 'H',  'e',  'l', 'l', 'o'};

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(consumed == 7);
    REQUIRE(frame.fin == true);
    REQUIRE(frame.opcode == WebSocketOpcode::TEXT);
    REQUIRE(frame.masked == false);
    REQUIRE(frame.payload_length == 5);
    REQUIRE(std::string(reinterpret_cast<const char*>(frame.payload.data()), 5) == "Hello");
}

TEST_CASE("WebSocket frame parser - Simple text frame (masked)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Text frame: FIN=1, opcode=1 (text), MASK=1, len=5
    // Masking key: 0x12345678
    // Payload: "Hello" masked
    // 'H' (0x48) XOR 0x12 = 0x5a
    // 'e' (0x65) XOR 0x34 = 0x51
    // 'l' (0x6c) XOR 0x56 = 0x3a
    // 'l' (0x6c) XOR 0x78 = 0x14
    // 'o' (0x6f) XOR 0x12 = 0x7d
    std::vector<uint8_t> data = {
        0x81, 0x85,                   // FIN=1, opcode=1, MASK=1, len=5
        0x12, 0x34, 0x56, 0x78,       // Masking key
        0x5a, 0x51, 0x3a, 0x14, 0x7d  // Masked "Hello" (corrected)
    };

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(consumed == 11);
    REQUIRE(frame.fin == true);
    REQUIRE(frame.opcode == WebSocketOpcode::TEXT);
    REQUIRE(frame.masked == true);
    REQUIRE(frame.masking_key == 0x12345678);
    REQUIRE(frame.payload_length == 5);

    // Unmask the payload to verify
    std::vector<uint8_t> payload_copy(frame.payload.begin(), frame.payload.end());
    WebSocketUtils::unmask_payload(payload_copy, frame.masking_key);
    REQUIRE(std::string(reinterpret_cast<const char*>(payload_copy.data()), 5) == "Hello");
}

TEST_CASE("WebSocket frame parser - Binary frame", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {0x82, 0x04,  // FIN=1, opcode=2 (binary), MASK=0, len=4
                                 0x01, 0x02, 0x03, 0x04};

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.opcode == WebSocketOpcode::BINARY);
    REQUIRE(frame.payload_length == 4);
    REQUIRE(frame.payload[0] == 0x01);
    REQUIRE(frame.payload[3] == 0x04);
}

TEST_CASE("WebSocket frame parser - Ping frame (control frame)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {0x89, 0x04,  // FIN=1, opcode=9 (ping), MASK=0, len=4
                                 'p',  'i',  'n', 'g'};

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.opcode == WebSocketOpcode::PING);
    REQUIRE(frame.is_control_frame() == true);
    REQUIRE(frame.payload_length == 4);
}

TEST_CASE("WebSocket frame parser - Pong frame", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {0x8A, 0x04,  // FIN=1, opcode=10 (pong), MASK=0, len=4
                                 'p',  'o',  'n', 'g'};

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.opcode == WebSocketOpcode::PONG);
    REQUIRE(frame.is_control_frame() == true);
}

TEST_CASE("WebSocket frame parser - Close frame with status code", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {
        0x88, 0x0C,  // FIN=1, opcode=8 (close), MASK=0, len=12
        0x03, 0xE8,  // Status code 1000 (normal closure) - 2 bytes
        'G',  'o',  'i', 'n', 'g', ' ', 'a', 'w', 'a', 'y'  // Reason - 10 bytes (total 12)
    };

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.opcode == WebSocketOpcode::CLOSE);
    REQUIRE(frame.payload_length == 12);

    // Extract status code (big-endian)
    uint16_t status_code = (frame.payload[0] << 8) | frame.payload[1];
    REQUIRE(status_code == 1000);
}

TEST_CASE("WebSocket frame parser - Fragmented message (3 frames)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Frame 1: FIN=0, opcode=1 (text), payload="Hel"
    std::vector<uint8_t> frame1 = {0x01, 0x03,  // FIN=0, opcode=1
                                   'H', 'e', 'l'};

    auto result1 = parser.parse(frame1, frame, consumed);
    REQUIRE(result1 == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.fin == false);
    REQUIRE(frame.opcode == WebSocketOpcode::TEXT);

    // Frame 2: FIN=0, opcode=0 (continuation), payload="lo "
    parser.reset();
    std::vector<uint8_t> frame2 = {0x00, 0x03,  // FIN=0, opcode=0 (continuation)
                                   'l', 'o', ' '};

    auto result2 = parser.parse(frame2, frame, consumed);
    REQUIRE(result2 == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.fin == false);
    REQUIRE(frame.opcode == WebSocketOpcode::CONTINUATION);

    // Frame 3: FIN=1, opcode=0 (continuation), payload="World"
    parser.reset();
    std::vector<uint8_t> frame3 = {0x80, 0x05,  // FIN=1, opcode=0 (continuation)
                                   'W',  'o',  'r', 'l', 'd'};

    auto result3 = parser.parse(frame3, frame, consumed);
    REQUIRE(result3 == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.fin == true);
    REQUIRE(frame.opcode == WebSocketOpcode::CONTINUATION);
}

TEST_CASE("WebSocket frame parser - Extended payload length (16-bit)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Create a frame with payload length = 200 (requires 16-bit extended length)
    std::vector<uint8_t> data = {
        0x81,
        126,  // FIN=1, opcode=1, len=126 (signals 16-bit length follows)
        0x00,
        0xC8,  // Extended length = 200 (big-endian)
    };

    // Add 200 bytes of payload
    data.resize(data.size() + 200, 'A');

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.payload_length == 200);
    REQUIRE(consumed == 4 + 200);
}

TEST_CASE("WebSocket frame parser - Extended payload length (64-bit)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Create a frame with payload length = 70000 (requires 64-bit extended length)
    std::vector<uint8_t> data = {
        0x81, 127,  // FIN=1, opcode=1, len=127 (signals 64-bit length follows)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x11, 0x70  // Extended length = 70000
    };

    // Add 70000 bytes of payload
    data.resize(data.size() + 70000, 'B');

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.payload_length == 70000);
    REQUIRE(consumed == 10 + 70000);
}

TEST_CASE("WebSocket frame parser - Invalid opcode (reserved)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {
        0x83, 0x00  // FIN=1, opcode=3 (reserved data opcode)
    };

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Error);
}

TEST_CASE("WebSocket frame parser - Fragmented control frame (invalid)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {
        0x08, 0x02,  // FIN=0, opcode=8 (close) - control frames MUST NOT be fragmented
        0x03, 0xE8};

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Error);
}

TEST_CASE("WebSocket frame parser - Partial frame (incremental parsing)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    // Full frame: Text "Hello"
    std::vector<uint8_t> full_frame = {0x81, 0x05,  // Header
                                       'H',  'e',  'l', 'l', 'o'};

    // Feed only first byte
    auto result1 = parser.parse(std::span<const uint8_t>(full_frame.data(), 1), frame, consumed);
    REQUIRE(result1 == WebSocketFrameParser::ParseResult::Incomplete);
    REQUIRE(consumed == 1);

    // Feed second byte
    auto result2 =
        parser.parse(std::span<const uint8_t>(full_frame.data() + 1, 1), frame, consumed);
    REQUIRE(result2 == WebSocketFrameParser::ParseResult::Incomplete);

    // Feed remaining bytes
    auto result3 =
        parser.parse(std::span<const uint8_t>(full_frame.data() + 2, 5), frame, consumed);
    REQUIRE(result3 == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(std::string(reinterpret_cast<const char*>(frame.payload.data()), 5) == "Hello");
}

TEST_CASE("WebSocket frame parser - Control frame too large (>125 bytes)", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {
        0x88, 126,  // FIN=1, opcode=8 (close), len=126 (extended length)
        0x00, 0x80  // Length = 128 bytes (> 125, invalid for control frames)
    };

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Error);
}

TEST_CASE("WebSocket frame parser - Empty payload", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data = {
        0x81, 0x00  // FIN=1, opcode=1 (text), len=0
    };

    auto result = parser.parse(data, frame, consumed);

    REQUIRE(result == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(frame.payload_length == 0);
    REQUIRE(frame.payload.empty());
}

TEST_CASE("WebSocket frame parser - Reset after complete", "[websocket][parser]") {
    WebSocketFrameParser parser;
    WebSocketFrame frame;
    size_t consumed = 0;

    std::vector<uint8_t> data1 = {0x81, 0x03, 'F', 'o', 'o'};

    auto result1 = parser.parse(data1, frame, consumed);
    REQUIRE(result1 == WebSocketFrameParser::ParseResult::Complete);

    // Reset and parse another frame
    parser.reset();

    std::vector<uint8_t> data2 = {0x81, 0x03, 'B', 'a', 'r'};

    auto result2 = parser.parse(data2, frame, consumed);
    REQUIRE(result2 == WebSocketFrameParser::ParseResult::Complete);
    REQUIRE(std::string(reinterpret_cast<const char*>(frame.payload.data()), 3) == "Bar");
}

// ========================================
// WebSocket Utility Function Tests
// ========================================

TEST_CASE("WebSocket utils - Unmask payload", "[websocket][utils]") {
    std::vector<uint8_t> payload = {0x5a, 0x51, 0x3a, 0x14, 0x7d};  // Corrected masked "Hello"
    uint32_t masking_key = 0x12345678;

    WebSocketUtils::unmask_payload(payload, masking_key);

    // Unmasked payload should be "Hello"
    REQUIRE(std::string(reinterpret_cast<const char*>(payload.data()), 5) == "Hello");
}

TEST_CASE("WebSocket utils - Create close frame", "[websocket][utils]") {
    auto close_frame =
        WebSocketUtils::create_close_frame(WebSocketCloseCode::NORMAL_CLOSURE, "Goodbye");

    REQUIRE(close_frame.size() >= 2);  // At least header + status code

    // Verify header: FIN=1, opcode=8
    REQUIRE((close_frame[0] & 0x80) != 0);  // FIN bit
    REQUIRE((close_frame[0] & 0x0F) == WebSocketOpcode::CLOSE);

    // Verify status code (big-endian 1000)
    size_t payload_offset = 2;  // After 2-byte header
    REQUIRE(close_frame[payload_offset] == 0x03);
    REQUIRE(close_frame[payload_offset + 1] == 0xE8);
}

TEST_CASE("WebSocket utils - Create ping frame", "[websocket][utils]") {
    auto ping_frame = WebSocketUtils::create_ping_frame();

    REQUIRE(ping_frame.size() == 2);  // Header only (no payload)

    // Verify header: FIN=1, opcode=9, len=0
    REQUIRE((ping_frame[0] & 0x80) != 0);  // FIN bit
    REQUIRE((ping_frame[0] & 0x0F) == WebSocketOpcode::PING);
    REQUIRE((ping_frame[1] & 0x7F) == 0);  // Payload length = 0
}

TEST_CASE("WebSocket utils - Create pong frame with payload", "[websocket][utils]") {
    std::vector<uint8_t> ping_payload = {'d', 'a', 't', 'a'};
    auto pong_frame = WebSocketUtils::create_pong_frame(ping_payload);

    REQUIRE(pong_frame.size() == 2 + 4);  // Header + payload

    // Verify header: FIN=1, opcode=10, len=4
    REQUIRE((pong_frame[0] & 0x80) != 0);  // FIN bit
    REQUIRE((pong_frame[0] & 0x0F) == WebSocketOpcode::PONG);
    REQUIRE((pong_frame[1] & 0x7F) == 4);  // Payload length = 4

    // Verify payload echoed back
    REQUIRE(pong_frame[2] == 'd');
    REQUIRE(pong_frame[5] == 'a');
}
