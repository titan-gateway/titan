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

// Titan WebSocket - Header
// WebSocket protocol support (RFC 6455)

#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace titan::http {

// Forward declarations
struct Request;

/// WebSocket connection state (RFC 6455 §4)
enum class WebSocketState : uint8_t {
    CONNECTING,  // Handshake in progress
    OPEN,        // Ready for data frames
    CLOSING,     // Close frame sent, waiting for response
    CLOSED       // Connection terminated
};

/// WebSocket frame opcodes (RFC 6455 §5.2)
namespace WebSocketOpcode {
constexpr uint8_t CONTINUATION = 0x0;  // Continuation frame
constexpr uint8_t TEXT = 0x1;          // Text frame (UTF-8)
constexpr uint8_t BINARY = 0x2;        // Binary frame
constexpr uint8_t CLOSE = 0x8;         // Connection close
constexpr uint8_t PING = 0x9;          // Ping (heartbeat request)
constexpr uint8_t PONG = 0xA;          // Pong (heartbeat response)
}  // namespace WebSocketOpcode

/// WebSocket close status codes (RFC 6455 §7.4)
namespace WebSocketCloseCode {
constexpr uint16_t NORMAL_CLOSURE = 1000;         // Normal closure
constexpr uint16_t GOING_AWAY = 1001;             // Server shutdown / browser navigate
constexpr uint16_t PROTOCOL_ERROR = 1002;         // Protocol violation
constexpr uint16_t UNSUPPORTED_DATA = 1003;       // Received unsupported data
constexpr uint16_t NO_STATUS_RECEIVED = 1005;     // No close code present (reserved)
constexpr uint16_t ABNORMAL_CLOSURE = 1006;       // Abnormal close (reserved)
constexpr uint16_t INVALID_FRAME_PAYLOAD = 1007;  // Invalid UTF-8 in text frame
constexpr uint16_t POLICY_VIOLATION = 1008;       // Policy violated
constexpr uint16_t MESSAGE_TOO_BIG = 1009;        // Message too big
constexpr uint16_t MANDATORY_EXTENSION = 1010;    // Expected extension not negotiated
constexpr uint16_t INTERNAL_SERVER_ERROR = 1011;  // Server error
constexpr uint16_t TLS_HANDSHAKE_FAILED = 1015;   // TLS handshake failed (reserved)
}  // namespace WebSocketCloseCode

/// WebSocket frame structure (RFC 6455 §5.2)
struct WebSocketFrame {
    bool fin = false;                  // Final fragment flag
    uint8_t opcode = 0;                // Frame opcode (see WebSocketOpcode)
    bool masked = false;               // Is payload masked?
    uint32_t masking_key = 0;          // XOR masking key (client→server only)
    uint64_t payload_length = 0;       // Payload size in bytes
    std::span<const uint8_t> payload;  // Frame payload (view into buffer)

    /// Check if this is a control frame
    [[nodiscard]] constexpr bool is_control_frame() const noexcept {
        return opcode >= 0x8;  // Control frames: 0x8-0xF
    }

    /// Check if this is a data frame
    [[nodiscard]] constexpr bool is_data_frame() const noexcept {
        return opcode < 0x8 && opcode <= 0x2;  // Data frames: 0x0-0x2
    }
};

/// WebSocket connection tracking
struct WebSocketConnection {
    int client_fd = -1;                                 // Client socket FD
    int backend_fd = -1;                                // Backend socket FD
    WebSocketState state = WebSocketState::CONNECTING;  // Connection state

    // Frame parsing state (accumulate partial frames)
    std::vector<uint8_t> client_frame_buffer;   // Partial frame from client
    std::vector<uint8_t> backend_frame_buffer;  // Partial frame from backend
    uint8_t current_opcode = 0;                 // Opcode of fragmented message
    bool fin_received = false;                  // Fragmentation state
    uint64_t accumulated_message_size = 0;      // Total size of fragmented message

    // Ping/Pong keep-alive state
    std::chrono::steady_clock::time_point last_activity;
    std::chrono::steady_clock::time_point last_ping_sent;
    bool pong_pending = false;

    // Statistics
    uint64_t frames_sent = 0;
    uint64_t frames_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    std::chrono::steady_clock::time_point connected_at;

    // Route configuration (for per-route limits and settings)
    std::string route_path;
    std::string upstream_name;
};

/// WebSocket handshake validation and utilities
class WebSocketUtils {
public:
    /// Compute Sec-WebSocket-Accept header value (RFC 6455 §4.2.2)
    /// Accept-Value = Base64(SHA1(Key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
    [[nodiscard]] static std::string compute_accept_key(std::string_view sec_websocket_key);

    /// Validate WebSocket upgrade request headers
    [[nodiscard]] static bool is_valid_upgrade_request(const Request& request);

    /// Create 101 Switching Protocols response
    [[nodiscard]] static std::string create_upgrade_response(std::string_view accept_key,
                                                             std::string_view protocol = "");

    /// Unmask WebSocket payload (client→server frames)
    static void unmask_payload(std::span<uint8_t> payload, uint32_t masking_key);

    /// Create WebSocket close frame
    [[nodiscard]] static std::vector<uint8_t> create_close_frame(uint16_t status_code,
                                                                 std::string_view reason);

    /// Create WebSocket pong frame (response to ping)
    [[nodiscard]] static std::vector<uint8_t> create_pong_frame(
        std::span<const uint8_t> ping_payload);

    /// Create WebSocket ping frame
    [[nodiscard]] static std::vector<uint8_t> create_ping_frame();

    /// Encode WebSocket frame header
    static void encode_frame_header(std::vector<uint8_t>& buffer, bool fin, uint8_t opcode,
                                    bool mask, uint64_t payload_length, uint32_t masking_key = 0);
};

/// WebSocket frame parser
class WebSocketFrameParser {
public:
    /// Parse result
    enum class ParseResult {
        Complete,    // Full frame parsed successfully
        Incomplete,  // Need more data (partial frame)
        Error        // Protocol violation (close connection)
    };

    WebSocketFrameParser() = default;
    ~WebSocketFrameParser() = default;

    /// Parse WebSocket frame from data
    /// @param data Input data buffer
    /// @param out_frame Parsed frame (only valid if result is Complete)
    /// @param consumed Number of bytes consumed from input
    /// @return Parse result status
    [[nodiscard]] ParseResult parse(std::span<const uint8_t> data, WebSocketFrame& out_frame,
                                    size_t& consumed);

    /// Reset parser state (for connection reuse or error recovery)
    void reset();

    /// Get current parser state for debugging
    [[nodiscard]] const char* state_name() const noexcept;

private:
    /// Parsing state machine
    enum class State {
        ReadHeader,         // Reading initial 2 bytes
        ReadExtendedLen16,  // Reading 16-bit extended length
        ReadExtendedLen64,  // Reading 64-bit extended length
        ReadMaskingKey,     // Reading 4-byte masking key
        ReadPayload,        // Reading payload data
        Complete            // Frame fully parsed
    };

    State state_ = State::ReadHeader;
    std::vector<uint8_t> buffer_;  // Accumulate partial frame data

    // Current frame being parsed
    bool fin_ = false;
    uint8_t opcode_ = 0;
    bool masked_ = false;
    uint64_t payload_length_ = 0;
    uint32_t masking_key_ = 0;
    size_t header_size_ = 0;  // Total header size (for payload offset)
};

}  // namespace titan::http
