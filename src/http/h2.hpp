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

// Titan HTTP/2 - Header
// HTTP/2 session management and multiplexing

#pragma once

#include <nghttp2/nghttp2.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "../core/containers.hpp"
#include "http.hpp"

namespace titan::http {

/// HTTP/2 connection preface (24 bytes)
/// "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
constexpr const char* HTTP2_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr size_t HTTP2_PREFACE_LEN = 24;

/// Detect if data starts with HTTP/2 connection preface
[[nodiscard]] bool is_http2_connection(std::span<const uint8_t> data) noexcept;

/// HTTP/2 stream state
enum class H2StreamState : uint8_t {
    Idle,              // Stream ID allocated but not used
    Open,              // Stream is open for sending/receiving
    HalfClosedLocal,   // Local end closed
    HalfClosedRemote,  // Remote end closed
    Closed,            // Stream fully closed
};

/// HTTP/2 stream representing a single request/response
struct H2Stream {
    int32_t stream_id = -1;
    H2StreamState state = H2StreamState::Idle;

    Request request;
    Response response;

    std::vector<uint8_t> request_body;   // Accumulated request body data
    std::vector<uint8_t> response_body;  // Accumulated response body data
    size_t response_body_offset = 0;     // Offset for chunked response body sending

    // Storage for HTTP/2 pseudo-headers (request.path/uri are views into these)
    std::string path_storage;  // Owned storage for :path pseudo-header
    std::string uri_storage;   // Owned storage for full URI

    // Storage for request header strings (request.headers views point into these)
    std::vector<std::pair<std::string, std::string>> request_header_storage;

    // Storage for response header strings (response.headers views point into these)
    std::vector<std::pair<std::string, std::string>> response_header_storage;

    // Storage for :status pseudo-header value (must persist during nghttp2_session_send)
    std::string status_storage;

    // Data provider for response body (must persist during nghttp2_session_send)
    nghttp2_data_provider data_provider;

    bool request_complete = false;
    bool response_complete = false;
};

/// HTTP/2 session managing multiple streams over a single connection
class H2Session {
public:
    /// Callback invoked when a stream is closed by nghttp2
    using StreamCloseCallback = std::function<void(int32_t stream_id)>;

    /// Create HTTP/2 session
    /// is_server: true for server mode, false for client mode
    explicit H2Session(bool is_server);
    ~H2Session();

    H2Session(const H2Session&) = delete;
    H2Session& operator=(const H2Session&) = delete;

    /// Process incoming HTTP/2 data (frames)
    /// Returns number of bytes consumed
    [[nodiscard]] std::error_code recv(std::span<const uint8_t> data, size_t& consumed);

    /// Get data to send (serialized HTTP/2 frames)
    /// Returns span of data ready to send
    [[nodiscard]] std::span<const uint8_t> send_data();

    /// Mark send buffer as consumed
    void consume_send_buffer(size_t bytes);

    /// Submit request (client mode)
    [[nodiscard]] std::error_code submit_request(const Request& request, int32_t& stream_id);

    /// Submit response (server mode)
    [[nodiscard]] std::error_code submit_response(int32_t stream_id, const Response& response);

    /// Get stream by ID
    [[nodiscard]] H2Stream* get_stream(int32_t stream_id);

    /// Get all active streams
    [[nodiscard]] std::vector<H2Stream*> get_active_streams();

    /// Check if session wants to send data
    [[nodiscard]] bool want_write() const noexcept;

    /// Check if connection should be closed
    [[nodiscard]] bool should_close() const noexcept;

    /// Set callback to be invoked when streams are closed
    void set_stream_close_callback(StreamCloseCallback callback);

private:
    bool is_server_;
    nghttp2_session* session_ = nullptr;

    titan::core::fast_map<int32_t, std::unique_ptr<H2Stream>> streams_;
    std::vector<uint8_t> send_buffer_;

    bool should_close_ = false;

    // Callback invoked when streams are closed
    StreamCloseCallback stream_close_callback_;

    // nghttp2 callbacks
    static ssize_t send_callback(nghttp2_session* session, const uint8_t* data, size_t length,
                                 int flags, void* user_data);

    static int on_frame_recv_callback(nghttp2_session* session, const nghttp2_frame* frame,
                                      void* user_data);

    static int on_stream_close_callback(nghttp2_session* session, int32_t stream_id,
                                        uint32_t error_code, void* user_data);

    static int on_header_callback(nghttp2_session* session, const nghttp2_frame* frame,
                                  const uint8_t* name, size_t namelen, const uint8_t* value,
                                  size_t valuelen, uint8_t flags, void* user_data);

    static int on_data_chunk_recv_callback(nghttp2_session* session, uint8_t flags,
                                           int32_t stream_id, const uint8_t* data, size_t len,
                                           void* user_data);

    // Helper methods
    H2Stream& get_or_create_stream(int32_t stream_id);
    void remove_stream(int32_t stream_id);
};

}  // namespace titan::http
