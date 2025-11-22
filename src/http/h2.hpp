// Titan HTTP/2 - Header
// HTTP/2 session management and multiplexing

#pragma once

#include "http.hpp"

#include <nghttp2/nghttp2.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace titan::http {

/// HTTP/2 connection preface (24 bytes)
/// "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
constexpr const char* HTTP2_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr size_t HTTP2_PREFACE_LEN = 24;

/// Detect if data starts with HTTP/2 connection preface
[[nodiscard]] bool is_http2_connection(std::span<const uint8_t> data) noexcept;

/// HTTP/2 stream state
enum class H2StreamState : uint8_t {
    Idle,           // Stream ID allocated but not used
    Open,           // Stream is open for sending/receiving
    HalfClosedLocal,  // Local end closed
    HalfClosedRemote, // Remote end closed
    Closed,         // Stream fully closed
};

/// HTTP/2 stream representing a single request/response
struct H2Stream {
    int32_t stream_id = -1;
    H2StreamState state = H2StreamState::Idle;

    Request request;
    Response response;

    std::vector<uint8_t> request_body;   // Accumulated request body data
    std::vector<uint8_t> response_body;  // Accumulated response body data

    bool request_complete = false;
    bool response_complete = false;
};

/// HTTP/2 session managing multiple streams over a single connection
class H2Session {
public:
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

private:
    bool is_server_;
    nghttp2_session* session_ = nullptr;

    std::unordered_map<int32_t, std::unique_ptr<H2Stream>> streams_;
    std::vector<uint8_t> send_buffer_;

    bool should_close_ = false;

    // nghttp2 callbacks
    static ssize_t send_callback(nghttp2_session* session, const uint8_t* data,
                                  size_t length, int flags, void* user_data);

    static int on_frame_recv_callback(nghttp2_session* session,
                                       const nghttp2_frame* frame, void* user_data);

    static int on_stream_close_callback(nghttp2_session* session, int32_t stream_id,
                                         uint32_t error_code, void* user_data);

    static int on_header_callback(nghttp2_session* session, const nghttp2_frame* frame,
                                   const uint8_t* name, size_t namelen,
                                   const uint8_t* value, size_t valuelen,
                                   uint8_t flags, void* user_data);

    static int on_data_chunk_recv_callback(nghttp2_session* session, uint8_t flags,
                                            int32_t stream_id, const uint8_t* data,
                                            size_t len, void* user_data);

    // Helper methods
    H2Stream& get_or_create_stream(int32_t stream_id);
    void remove_stream(int32_t stream_id);
};

} // namespace titan::http
