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

// Forward declaration for request body callback
static ssize_t request_data_read_callback(nghttp2_session* session, int32_t stream_id, uint8_t* buf,
                                          size_t length, uint32_t* data_flags, nghttp2_data_source* source,
                                          void* user_data);

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
    nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, on_frame_send_callback);
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
    // Use conservative defaults for maximum compatibility
    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},  // Disable server push for client mode
    };
    nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, settings, 3);
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

    int iterations = 0;
    const int max_iterations = 100;  // Safety limit
    while (nghttp2_session_want_write(session_) && iterations < max_iterations) {
        int rv = nghttp2_session_send(session_);
        iterations++;

        if (rv != 0) {
            // Error occurred
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
    std::string scheme = "http";  // Use http for h2c (cleartext HTTP/2), https for TLS

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":method")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(method_str.c_str())),
                       7, method_str.size(), NGHTTP2_NV_FLAG_NONE});

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":path")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(path.c_str())), 5,
                       path.size(), NGHTTP2_NV_FLAG_NONE});

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":scheme")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(scheme.c_str())), 7,
                       scheme.size(), NGHTTP2_NV_FLAG_NONE});

    // :authority pseudo-header (REQUIRED for HTTP/2, equivalent to Host header)
    std::string authority;
    for (const auto& header : request.headers) {
        if (header.name == "host" || header.name == "Host") {
            authority = std::string(header.value);
            break;
        }
    }
    if (authority.empty()) {
        authority = "localhost";  // Fallback if no Host header
    }

    headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":authority")),
                       const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(authority.c_str())), 10,
                       authority.size(), NGHTTP2_NV_FLAG_NONE});

    // Regular headers (skip Host since it's now :authority)
    for (const auto& header : request.headers) {
        // Skip Host header (already added as :authority)
        if (header.name == "host" || header.name == "Host") {
            continue;
        }
        headers.push_back(
            {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.name.data())),
             const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(header.value.data())),
             header.name.size(), header.value.size(), NGHTTP2_NV_FLAG_NONE});
    }

    // CRITICAL: Create stream BEFORE submitting request (nghttp2 may call data callback during submit)
    // We use a placeholder negative ID that will be replaced with the real stream ID
    static int32_t temp_stream_id_counter = -1000;
    int32_t temp_id = temp_stream_id_counter--;

    H2Stream& temp_stream = get_or_create_stream(temp_id);
    temp_stream.request = request;
    temp_stream.stream_id = temp_id;

    // Prepare data provider for request body (if present)
    nghttp2_data_provider* data_prd = nullptr;
    nghttp2_data_provider data_provider;

    // CRITICAL: For gRPC, we must send the request body!
    // The body contains the protobuf-encoded message
    if (!request.body.empty()) {
        // Copy request body to stream storage
        temp_stream.request_body.assign(request.body.begin(), request.body.end());

        // Setup data provider to send body
        data_provider.source.ptr = &temp_stream;
        data_provider.read_callback = request_data_read_callback;  // Use request callback, not response
        data_prd = &data_provider;
    }

    // Submit request
    int32_t sid =
        nghttp2_submit_request(session_, nullptr, headers.data(), headers.size(), data_prd, nullptr);

    if (sid < 0) {
        // Clean up temporary stream on error
        streams_.erase(temp_id);
        return std::make_error_code(std::errc::protocol_error);
    }

    stream_id = sid;

    // Move stream from temporary ID to real ID
    // CRITICAL: Set stream_id BEFORE moving to avoid dangling reference
    temp_stream.stream_id = sid;  // temp_stream is still valid here
    auto moved_stream = std::move(streams_[temp_id]);  // Move to local variable
    streams_.erase(temp_id);  // Erase old entry
    streams_[sid] = std::move(moved_stream);  // Insert with new ID

    return {};
}

// Static data read callback for nghttp2 (response body - for server mode)
static ssize_t data_read_callback(nghttp2_session* /*session*/, int32_t stream_id, uint8_t* buf,
                                  size_t length, uint32_t* data_flags, nghttp2_data_source* source,
                                  void* /*user_data*/) {
    auto* stream = static_cast<http::H2Stream*>(source->ptr);

    if (!stream || stream->response_body.empty()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }

    // Calculate how much data remains to send
    size_t remaining = stream->response_body.size() - stream->response_body_offset;
    if (remaining == 0) {
        // All data has been sent
        // CRITICAL: If trailers exist, return DEFERRED instead of EOF
        // This tells nghttp2 to stop calling the data callback and wait for submit_trailers()
        if (!stream->trailers.empty()) {
            return NGHTTP2_ERR_DEFERRED;  // Stop data callback, trailers will be submitted manually
        } else {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }
        return 0;
    }

    // Copy up to 'length' bytes from current offset
    size_t to_copy = std::min(length, remaining);
    std::memcpy(buf, stream->response_body.data() + stream->response_body_offset, to_copy);

    // Advance offset
    stream->response_body_offset += to_copy;

    // Only set EOF if ALL data has been sent
    if (stream->response_body_offset >= stream->response_body.size()) {
        // CRITICAL: For gRPC, if trailers exist, set NO_END_STREAM instead of EOF
        // This tells nghttp2 to send DATA frame but keep stream open for trailers
        if (!stream->trailers.empty()) {
            *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        } else {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }
    }

    return static_cast<ssize_t>(to_copy);
}

// Static data read callback for sending request body to backend (client mode)
static ssize_t request_data_read_callback(nghttp2_session* /*session*/, int32_t stream_id, uint8_t* buf,
                                          size_t length, uint32_t* data_flags, nghttp2_data_source* source,
                                          void* /*user_data*/) {
    auto* stream = static_cast<http::H2Stream*>(source->ptr);

    if (!stream || stream->request_body.empty()) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        return 0;
    }

    // For request bodies, we send all data at once (no chunking)
    // The offset tracking is handled internally
    size_t to_copy = std::min(length, stream->request_body.size());
    std::memcpy(buf, stream->request_body.data(), to_copy);

    // Always set EOF for request body (complete in one call)
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;

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

    // Get stream first (need it to store headers)
    auto* stream = get_stream(stream_id);
    if (!stream) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // CRITICAL FIX: Prevent duplicate response submissions (causes duplicate headers)
    if (stream->response_submitted) {
        return {};  // Success - already submitted
    }

    // Convert HTTP/1.1 Response to HTTP/2 headers
    std::vector<nghttp2_nv> headers;

    // :status pseudo-header - STORE IN STREAM to persist during async send
    stream->status_storage = std::to_string(static_cast<int>(response.status));
    headers.push_back(
        {const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":status")),
         const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(stream->status_storage.c_str())), 7,
         stream->status_storage.size(), NGHTTP2_NV_FLAG_NONE});

    // Regular headers - use stream's owned storage (for concurrent stream safety)
    // NOTE: handle_backend_event() already populates response_header_storage for backend responses.
    // For direct responses (404, middleware errors, etc.), we need to populate it here.
    if (stream->response_header_storage.empty()) {
        // First submission - copy headers to stream storage
        for (auto it = response.all_headers_begin(); it != response.all_headers_end(); ++it) {
            auto [name, value] = *it;
            if (name.empty() || value.empty()) {
                continue;  // Skip empty headers
            }
            stream->response_header_storage.emplace_back(std::string(name), std::string(value));
        }
    }

    // Build nghttp2_nv array from stream's owned storage (stable pointers)
    for (const auto& [name, value] : stream->response_header_storage) {
        headers.push_back({const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name.data())),
                           const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(value.data())),
                           name.size(), value.size(), NGHTTP2_NV_FLAG_NONE});
    }

    // Prepare data provider if body exists
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

    // Mark response as submitted to prevent duplicate calls
    stream->response_submitted = true;

    return {};
}

std::error_code H2Session::submit_trailers(int32_t stream_id) {
    if (!is_server_) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    auto* stream = get_stream(stream_id);
    if (!stream || stream->trailers.empty()) {
        return {};  // No trailers to submit
    }

    // Build nghttp2_nv array from trailers
    std::vector<nghttp2_nv> trailer_nvs;
    trailer_nvs.reserve(stream->trailers.size());

    for (const auto& [name, value] : stream->trailers) {
        trailer_nvs.push_back({
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(name.data())),
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(value.data())),
            name.size(), value.size(), NGHTTP2_NV_FLAG_NONE
        });
    }

    // Submit trailers (nghttp2 will send them as HEADERS frame with END_STREAM)
    int rv = nghttp2_submit_trailer(session_, stream_id, trailer_nvs.data(), trailer_nvs.size());

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

void H2Session::set_stream_close_callback(StreamCloseCallback callback) {
    stream_close_callback_ = std::move(callback);
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
                    // Check if this is a trailer (HEADERS frame with END_STREAM flag)
                    if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                        // This is a trailer frame (used by gRPC for status codes)
                        if (stream->is_grpc && !stream->trailers.empty()) {
                            // Extract gRPC status and message from trailers
                            stream->grpc_status = extract_grpc_status(stream->trailers);
                            stream->grpc_message = extract_grpc_message(stream->trailers);
                        }
                    }

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

int H2Session::on_frame_send_callback(nghttp2_session* /*session*/, const nghttp2_frame* /*frame*/,
                                      void* /*user_data*/) {
    return 0;
}

int H2Session::on_stream_close_callback(nghttp2_session* /*session*/, int32_t stream_id,
                                        uint32_t /*error_code*/, void* user_data) {
    auto* self = static_cast<H2Session*>(user_data);

    auto* stream = self->get_stream(stream_id);
    if (stream) {
        stream->state = H2StreamState::Closed;

        // Notify server to cleanup backend mappings BEFORE removing stream
        if (self->stream_close_callback_) {
            self->stream_close_callback_(stream_id);
        }

        // CRITICAL: For client sessions (is_server=false), DO NOT remove the stream yet!
        // The server code needs to extract the response first, then it will explicitly remove it.
        // Only server sessions should auto-remove streams to free memory for new requests.
        if (self->is_server_) {
            self->remove_stream(stream_id);
        }
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

    // Check if this is a trailer (HEADERS frame with END_STREAM flag)
    // Trailers don't have pseudo-headers (headers starting with ':')
    bool is_trailer = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) && (name_sv[0] != ':');

    if (self->is_server_) {
        // Parsing request headers
        if (name_sv == ":method") {
            stream.request.method = parse_method(value_sv);
        } else if (name_sv == ":path") {
            // Store path in owned storage (nghttp2 memory is temporary)
            stream.path_storage = std::string(value_sv);
            stream.request.path = stream.path_storage;
            stream.request.uri = stream.path_storage;  // For HTTP/2, uri = path

            // Parse gRPC method name if this is a gRPC request
            // Note: We may not know if it's gRPC yet (content-type header not seen)
            // So we parse opportunistically and validate with content-type later
            auto grpc_meta = parse_grpc_path(value_sv);
            if (grpc_meta) {
                stream.grpc_metadata = std::move(grpc_meta);
            }
        } else if (name_sv == ":scheme") {
            // Store scheme if needed
        } else if (name_sv == "content-type") {
            // Detect gRPC requests
            if (is_grpc_request(value_sv)) {
                stream.is_grpc = true;
                if (stream.grpc_metadata) {
                    stream.grpc_metadata->is_grpc_web = is_grpc_web_request(value_sv);
                }
            }
            // Store content-type header (or trailer if END_STREAM)
            if (is_trailer) {
                stream.trailers.emplace_back(std::string(name_sv), std::string(value_sv));
            } else {
                stream.request_header_storage.emplace_back(std::string(name_sv),
                                                           std::string(value_sv));
                const auto& [owned_name, owned_value] = stream.request_header_storage.back();
                stream.request.headers.push_back(Header{owned_name, owned_value});
            }
        } else if (name_sv[0] != ':') {
            // Regular header or trailer
            if (is_trailer) {
                // Store trailer
                stream.trailers.emplace_back(std::string(name_sv), std::string(value_sv));
            } else {
                // Regular header - store in owned storage first, then create views
                stream.request_header_storage.emplace_back(std::string(name_sv),
                                                           std::string(value_sv));
                const auto& [owned_name, owned_value] = stream.request_header_storage.back();
                stream.request.headers.push_back(Header{owned_name, owned_value});
            }
        }
    } else {
        // Parsing response headers
        if (name_sv == ":status") {
            int status_code = std::stoi(std::string(value_sv));
            stream.response.status = static_cast<StatusCode>(status_code);
        } else if (name_sv == "content-type") {
            // Detect gRPC responses
            if (is_grpc_request(value_sv)) {
                stream.is_grpc = true;
                if (stream.grpc_metadata) {
                    stream.grpc_metadata->is_grpc_web = is_grpc_web_request(value_sv);
                }
            }
            // Store content-type header (or trailer if END_STREAM)
            if (is_trailer) {
                stream.trailers.emplace_back(std::string(name_sv), std::string(value_sv));
            } else {
                stream.response_header_storage.emplace_back(std::string(name_sv),
                                                            std::string(value_sv));
                const auto& [owned_name, owned_value] = stream.response_header_storage.back();
                stream.response.headers.push_back(Header{owned_name, owned_value});
            }
        } else if (name_sv[0] != ':') {
            // Regular header or trailer
            if (is_trailer) {
                // Store trailer (gRPC uses trailers for status codes)
                stream.trailers.emplace_back(std::string(name_sv), std::string(value_sv));
            } else {
                // Regular header - store in owned storage first, then create views
                stream.response_header_storage.emplace_back(std::string(name_sv),
                                                            std::string(value_sv));
                const auto& [owned_name, owned_value] = stream.response_header_storage.back();
                stream.response.headers.push_back(Header{owned_name, owned_value});
            }
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
