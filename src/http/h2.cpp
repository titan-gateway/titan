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

// Titan HTTP/2 - Implementation
// HTTP/2 session management using nghttp2

#include "h2.hpp"

#include <cstring>
#include <iostream>
#include <system_error>

namespace titan::http {

// ============================
// HTTP/2 Detection
// ============================

bool is_http2_connection(std::span<const uint8_t> data) noexcept {
    if (data.size() < HTTP2_PREFACE_LEN) {
        return false;
    }

    return std::memcmp(data.data(), HTTP2_PREFACE, HTTP2_PREFACE_LEN) == 0;
}

// ============================
// H2Session Implementation
// ============================

H2Session::H2Session(bool is_server) : is_server_(is_server) {
    nghttp2_session_callbacks* callbacks;
    nghttp2_session_callbacks_new(&callbacks);

    // Register callbacks
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close_callback);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                              on_data_chunk_recv_callback);

    // Create session
    if (is_server_) {
        nghttp2_session_server_new(&session_, callbacks, this);
    } else {
        nghttp2_session_client_new(&session_, callbacks, this);
    }

    nghttp2_session_callbacks_del(callbacks);

    // Submit settings frame
    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
         1000},  // Increased from 100 to support heavy load
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
    };
    nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, settings, 2);
}

H2Session::~H2Session() {
    if (session_) {
        nghttp2_session_del(session_);
    }
}

std::error_code H2Session::recv(std::span<const uint8_t> data, size_t& consumed) {
    ssize_t readlen = nghttp2_session_mem_recv(session_, data.data(), data.size());

    if (readlen < 0) {
        return std::make_error_code(std::errc::protocol_error);
    }

    consumed = static_cast<size_t>(readlen);
    return {};
}

std::span<const uint8_t> H2Session::send_data() {
    // Trigger nghttp2 to serialize frames into send_buffer_
    send_buffer_.clear();

    while (nghttp2_session_want_write(session_)) {
        int rv = nghttp2_session_send(session_);
        if (rv != 0) {
            break;
        }
    }

    return std::span<const uint8_t>(send_buffer_);
}

void H2Session::consume_send_buffer(size_t bytes) {
    if (bytes >= send_buffer_.size()) {
        send_buffer_.clear();
    } else {
        send_buffer_.erase(send_buffer_.begin(), send_buffer_.begin() + bytes);
    }
}

std::error_code H2Session::submit_request(const Request& request, int32_t& stream_id) {
    if (is_server_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    // Convert HTTP/1.1 Request to HTTP/2 headers
    std::vector<nghttp2_nv> headers;

    // Pseudo-headers (required for HTTP/2)
    std::string method_str = std::string(to_string(request.method));
    std::string path = std::string(request.path);
    std::string scheme = "https";  // Use HTTPS for TLS connections (all HTTP/2 in production)

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":method")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(method_str.c_str())),
                       7, method_str.size(), NGHTTP2_NV_FLAG_NONE});

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":path")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(path.c_str())), 5,
                       path.size(), NGHTTP2_NV_FLAG_NONE});

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":scheme")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(scheme.c_str())), 7,
                       scheme.size(), NGHTTP2_NV_FLAG_NONE});

    // Regular headers
    for (const auto& header : request.headers) {
        headers.push_back(
            {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.name.data())),
             const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.value.data())),
             header.name.size(), header.value.size(), NGHTTP2_NV_FLAG_NONE});
    }

    // Submit request
    int32_t sid =
        nghttp2_submit_request(session_, nullptr, headers.data(), headers.size(), nullptr, nullptr);
    if (sid < 0) {
        return std::make_error_code(std::errc::protocol_error);
    }

    stream_id = sid;
    return {};
}

// Static data read callback for nghttp2 (with body data)
static ssize_t data_read_callback(nghttp2_session* /*session*/, int32_t /*stream_id*/, uint8_t* buf,
                                  size_t length, uint32_t* data_flags, nghttp2_data_source* source,
                                  void* /*user_data*/) {
    auto* stream = static_cast<http::H2Stream*>(source->ptr);

    if (!stream || stream->response_body.empty()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }

    size_t to_copy = std::min(length, stream->response_body.size());
    std::memcpy(buf, stream->response_body.data(), to_copy);
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;  // Send all data in one frame
    return static_cast<ssize_t>(to_copy);
}

// Static data read callback for empty body (END_STREAM only)
static ssize_t empty_data_callback(nghttp2_session* /*session*/, int32_t /*stream_id*/,
                                   uint8_t* /*buf*/, size_t /*length*/, uint32_t* data_flags,
                                   nghttp2_data_source* /*source*/, void* /*user_data*/) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return 0;
}

std::error_code H2Session::submit_response(int32_t stream_id, const Response& response) {
    if (!is_server_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    // Convert HTTP/1.1 Response to HTTP/2 headers
    std::vector<nghttp2_nv> headers;

    // :status pseudo-header
    std::string status = std::to_string(static_cast<int>(response.status));
    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":status")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(status.c_str())), 7,
                       status.size(), NGHTTP2_NV_FLAG_NONE});

    // Regular headers
    for (const auto& header : response.headers) {
        if (header.name.empty() || header.value.empty()) {
            continue;  // Skip empty headers
        }
        headers.push_back(
            {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.name.data())),
             const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.value.data())),
             header.name.size(), header.value.size(), NGHTTP2_NV_FLAG_NONE});
    }

    // Prepare data provider if body exists
    auto* stream = get_stream(stream_id);
    if (!stream) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Only copy body data if stream doesn't already have it
    // (handle_backend_event may have already moved it to stream->response_body)
    if (!response.body.empty() && stream->response_body.empty()) {
        // Store body in stream first (nghttp2 will read from it during send)
        stream->response_body.assign(response.body.begin(), response.body.end());
    }

    // Set up data provider based on whether we have body data
    if (!stream->response_body.empty()) {
        // Create data provider in stream (persists for lifetime of stream)
        stream->data_provider.source.ptr = stream;
        stream->data_provider.read_callback = data_read_callback;
    } else {
        // No body - use empty data callback
        stream->data_provider.source.ptr = nullptr;
        stream->data_provider.read_callback = empty_data_callback;
    }

    // Submit response with headers and persistent data provider
    int rv = nghttp2_submit_response(session_, stream_id, headers.data(), headers.size(),
                                     &stream->data_provider);
    if (rv != 0) {
        return std::make_error_code(std::errc::protocol_error);
    }

    return {};
}

H2Stream* H2Session::get_stream(int32_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<H2Stream*> H2Session::get_active_streams() {
    std::vector<H2Stream*> active;
    for (auto& [sid, stream] : streams_) {
        if (stream->state != H2StreamState::Closed) {
            active.push_back(stream.get());
        }
    }
    return active;
}

bool H2Session::want_write() const noexcept {
    return nghttp2_session_want_write(session_) || !send_buffer_.empty();
}

bool H2Session::should_close() const noexcept {
    return should_close_;
}

H2Stream& H2Session::get_or_create_stream(int32_t stream_id) {
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        return *it->second;
    }

    auto stream = std::make_unique<H2Stream>();
    stream->stream_id = stream_id;
    stream->state = H2StreamState::Open;

    // Reserve capacity to prevent reallocation (which would invalidate string_views)
    // Using 64 as a reasonable upper bound for typical requests (power of 2, ~4KB overhead)
    stream->request_header_storage.reserve(64);
    stream->response_header_storage.reserve(64);

    auto* ptr = stream.get();
    streams_[stream_id] = std::move(stream);
    return *ptr;
}

void H2Session::remove_stream(int32_t stream_id) {
    streams_.erase(stream_id);
}

// ============================
// nghttp2 Callbacks
// ============================

ssize_t H2Session::send_callback(nghttp2_session* /*session*/, const uint8_t* data, size_t length,
                                 int /*flags*/, void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    // Append to send buffer
    self->send_buffer_.insert(self->send_buffer_.end(), data, data + length);

    return static_cast<ssize_t>(length);
}

int H2Session::on_frame_recv_callback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                                      void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    switch (frame->hd.type) {
        case NGHTTP2_HEADERS:
            if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
                // Headers complete
                auto* stream = self->get_stream(frame->hd.stream_id);
                if (stream) {
                    if (self->is_server_) {
                        // Request headers received
                        stream->request_complete = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM);
                    } else {
                        // Response headers received
                        stream->response_complete = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM);
                    }
                }
            }
            break;

        case NGHTTP2_DATA:
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                auto* stream = self->get_stream(frame->hd.stream_id);
                if (stream) {
                    if (self->is_server_) {
                        stream->request_complete = true;
                    } else {
                        stream->response_complete = true;
                    }
                }
            }
            break;

        case NGHTTP2_GOAWAY:
            self->should_close_ = true;
            break;

        default:
            break;
    }

    return 0;
}

int H2Session::on_stream_close_callback(nghttp2_session* /*session*/, int32_t stream_id,
                                        uint32_t /*error_code*/, void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    auto* stream = self->get_stream(stream_id);
    if (stream) {
        stream->state = H2StreamState::Closed;
        // Immediately remove closed stream to free memory and allow new streams
        self->remove_stream(stream_id);
    }

    return 0;
}

int H2Session::on_header_callback(nghttp2_session* /*session*/, const nghttp2_frame* frame,
                                  const uint8_t* name, size_t namelen, const uint8_t* value,
                                  size_t valuelen, uint8_t /*flags*/, void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    if (frame->hd.type != NGHTTP2_HEADERS) {
        return 0;
    }

    auto& stream = self->get_or_create_stream(frame->hd.stream_id);

    std::string_view name_sv(reinterpret_cast<const char*>(name), namelen);
    std::string_view value_sv(reinterpret_cast<const char*>(value), valuelen);

    if (self->is_server_) {
        // Parsing request headers
        if (name_sv == ":method") {
            stream.request.method = parse_method(value_sv);
        } else if (name_sv == ":path") {
            // Store path in owned storage (nghttp2 memory is temporary)
            stream.path_storage = std::string(value_sv);
            stream.request.path = stream.path_storage;
            stream.request.uri = stream.path_storage;  // For HTTP/2, uri = path
        } else if (name_sv == ":scheme") {
            // Store scheme if needed
        } else if (name_sv[0] != ':') {
            // Regular header - store in owned storage first, then create views
            stream.request_header_storage.emplace_back(std::string(name_sv), std::string(value_sv));
            const auto& [owned_name, owned_value] = stream.request_header_storage.back();
            stream.request.headers.push_back(Header{owned_name, owned_value});
        }
    } else {
        // Parsing response headers
        if (name_sv == ":status") {
            int status_code = std::stoi(std::string(value_sv));
            stream.response.status = static_cast<StatusCode>(status_code);
        } else if (name_sv[0] != ':') {
            // Regular header - store in owned storage first, then create views
            stream.response_header_storage.emplace_back(std::string(name_sv),
                                                        std::string(value_sv));
            const auto& [owned_name, owned_value] = stream.response_header_storage.back();
            stream.response.headers.push_back(Header{owned_name, owned_value});
        }
    }

    return 0;
}

int H2Session::on_data_chunk_recv_callback(nghttp2_session* /*session*/, uint8_t /*flags*/,
                                           int32_t stream_id, const uint8_t* data, size_t len,
                                           void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    auto* stream = self->get_stream(stream_id);
    if (!stream) {
        return 0;
    }

    if (self->is_server_) {
        // Request body data
        stream->request_body.insert(stream->request_body.end(), data, data + len);
        stream->request.body = std::span<const uint8_t>(stream->request_body);
    } else {
        // Response body data
        stream->response_body.insert(stream->response_body.end(), data, data + len);
        stream->response.body = std::span<const uint8_t>(stream->response_body);
    }

    return 0;
}

}  // namespace titan::http
