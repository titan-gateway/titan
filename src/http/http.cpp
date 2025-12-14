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

// Titan HTTP Protocol - Implementation

#include "http.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>

namespace titan::http {

// Request helper methods

const Header* Request::find_header(std::string_view name) const noexcept {
    for (const auto& header : headers) {
        if (header_name_equals(header.name, name)) {
            return &header;
        }
    }
    return nullptr;
}

std::string_view Request::get_header(std::string_view name,
                                     std::string_view default_value) const noexcept {
    const Header* header = find_header(name);
    return header ? header->value : default_value;
}

bool Request::has_header(std::string_view name) const noexcept {
    return find_header(name) != nullptr;
}

size_t Request::content_length() const noexcept {
    auto value = get_header("Content-Length", "0");
    size_t length = 0;
    std::from_chars(value.data(), value.data() + value.size(), length);
    return length;
}

bool Request::keep_alive() const noexcept {
    auto connection = get_header("Connection");

    // HTTP/1.1 defaults to keep-alive
    if (version == Version::HTTP_1_1) {
        // Only close if explicitly requested
        return connection != "close";
    }

    // HTTP/1.0 defaults to close
    return connection == "keep-alive";
}

// Response helper methods

const Header* Response::find_header(std::string_view name) const noexcept {
    // Search backend headers first (zero-copy from upstream)
    for (const auto& header : backend_headers) {
        if (header_name_equals(header.name, name)) {
            return &header;
        }
    }

    // Search middleware headers (owned strings)
    // Note: We return a temporary Header pointing to the owned strings
    // This is safe because std::string is stable as long as the vector doesn't reallocate
    static thread_local Header temp_header;
    for (const auto& [hdr_name, hdr_value] : middleware_headers) {
        if (header_name_equals(hdr_name, name)) {
            temp_header.name = hdr_name;
            temp_header.value = hdr_value;
            return &temp_header;
        }
    }

    // Fallback: search deprecated headers field for backward compatibility
    for (const auto& header : headers) {
        if (header_name_equals(header.name, name)) {
            return &header;
        }
    }

    return nullptr;
}

std::string_view Response::get_header(std::string_view name,
                                      std::string_view default_value) const noexcept {
    const Header* header = find_header(name);
    return header ? header->value : default_value;
}

bool Response::has_header(std::string_view name) const noexcept {
    return find_header(name) != nullptr;
}

void Response::add_header(std::string_view name, std::string_view value) {
    // Hot path optimization: Use flat pre-allocated buffer instead of multiple heap allocations
    // Typical response adds 2-5 headers (~150-300 bytes total)
    // Single 2KB allocation handles 99% of cases without reallocation

    ensure_header_storage_capacity();  // Reserve 2KB on first use (inline, fast)

    // Capture old buffer pointer and size to detect reallocation
    const char* old_data = header_storage.data();
    const size_t old_size = header_storage.size();
    const char* old_end = old_data + old_size;

    // Append name to flat buffer and capture offset
    size_t name_offset = header_storage.size();
    header_storage.append(name);

    // Append value to flat buffer and capture offset
    size_t value_offset = header_storage.size();
    header_storage.append(value);

    // Detect reallocation by comparing buffer addresses
    if (header_storage.data() != old_data) {
        // Buffer was reallocated - fix ONLY string_views that point to old header_storage
        // CRITICAL: Some headers may point to response_header_storage (backend headers)
        // We must NOT corrupt those by calculating offsets from wrong base pointer!
        for (auto& h : headers) {
            // Check if name points into OLD header_storage range
            bool name_in_old_storage = (h.name.data() >= old_data && h.name.data() < old_end);
            if (name_in_old_storage) {
                size_t name_off = h.name.data() - old_data;
                h.name = {header_storage.data() + name_off, h.name.size()};
            }

            // Check if value points into OLD header_storage range
            bool value_in_old_storage = (h.value.data() >= old_data && h.value.data() < old_end);
            if (value_in_old_storage) {
                size_t value_off = h.value.data() - old_data;
                h.value = {header_storage.data() + value_off, h.value.size()};
            }
        }
    }

    // Create string_views pointing into the buffer
    // SAFE: If reallocation occurred, we fixed all existing string_views above
    std::string_view name_view{header_storage.data() + name_offset, name.size()};
    std::string_view value_view{header_storage.data() + value_offset, value.size()};

    headers.push_back({name_view, value_view});
}

bool Response::remove_header(std::string_view name) {
    bool found = false;

    // Remove from backend headers
    auto backend_it =
        std::remove_if(backend_headers.begin(), backend_headers.end(),
                       [name](const Header& h) { return header_name_equals(h.name, name); });
    if (backend_it != backend_headers.end()) {
        backend_headers.erase(backend_it, backend_headers.end());
        found = true;
    }

    // Remove from middleware headers
    auto middleware_it =
        std::remove_if(middleware_headers.begin(), middleware_headers.end(),
                       [name](const auto& pair) { return header_name_equals(pair.first, name); });
    if (middleware_it != middleware_headers.end()) {
        middleware_headers.erase(middleware_it, middleware_headers.end());
        found = true;
    }

    // Fallback: remove from deprecated headers field
    auto headers_it = std::remove_if(headers.begin(), headers.end(), [name](const Header& h) {
        return header_name_equals(h.name, name);
    });
    if (headers_it != headers.end()) {
        headers.erase(headers_it, headers.end());
        found = true;
    }

    return found;
}

bool Response::modify_header(std::string_view name, std::string_view new_value) {
    // Search middleware headers first (owned, safe to modify)
    for (auto& [hdr_name, hdr_value] : middleware_headers) {
        if (header_name_equals(hdr_name, name)) {
            hdr_value = std::string(new_value);  // Update owned string
            return true;
        }
    }

    // Search backend headers (zero-copy, can't modify in place)
    // If found in backend, remove it and add as middleware header
    for (size_t i = 0; i < backend_headers.size(); ++i) {
        if (header_name_equals(backend_headers[i].name, name)) {
            // Move to middleware headers with new value
            std::string name_str(backend_headers[i].name);
            backend_headers.erase(backend_headers.begin() + i);
            middleware_headers.emplace_back(std::move(name_str), std::string(new_value));
            return true;
        }
    }

    // Fallback: search deprecated headers field
    for (auto& header : headers) {
        if (header_name_equals(header.name, name)) {
            // Append new value to flat buffer
            ensure_header_storage_capacity();
            size_t value_offset = header_storage.size();
            header_storage.append(new_value);

            // Update header to point to new value in buffer
            header.value = std::string_view{header_storage.data() + value_offset, new_value.size()};
            return true;
        }
    }

    return false;  // Header not found
}

void Response::set_content_length(size_t length) {
    // TODO: Store in arena
    static thread_local char buffer[32];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), length);
    add_header("Content-Length", std::string_view(buffer, result.ptr - buffer));
}

void Response::set_content_type(std::string_view content_type) {
    add_header("Content-Type", content_type);
}

size_t Response::content_length() const noexcept {
    auto value = get_header("Content-Length", "0");
    size_t length = 0;
    std::from_chars(value.data(), value.data() + value.size(), length);
    return length;
}

bool Response::keep_alive() const noexcept {
    auto connection = get_header("Connection");

    // HTTP/1.1 defaults to keep-alive
    if (version == Version::HTTP_1_1) {
        // Only close if explicitly requested
        return connection != "close";
    }

    // HTTP/1.0 defaults to close
    return connection == "keep-alive";
}

// HYBRID STORAGE API (Phase 2)

void Response::add_backend_header(std::string_view name, std::string_view value) {
    // Zero-copy: Store string_views pointing to external storage (recv_buffer or
    // response_header_storage) CRITICAL: Caller must ensure lifetime (typically points to
    // recv_buffer)
    backend_headers.push_back({name, value});
}

void Response::add_middleware_header(std::string_view name, std::string_view value) {
    // Safe: Always copies to owned strings, no lifetime concerns
    // Cannot create dangling pointers - type system enforces correctness
    middleware_headers.emplace_back(std::string(name), std::string(value));
}

std::pair<std::string_view, std::string_view> Response::AllHeadersIterator::operator*() const {
    if (backend_idx < response->backend_headers.size()) {
        // Return backend header as string_view pair
        const auto& h = response->backend_headers[backend_idx];
        return {h.name, h.value};
    } else {
        // Return middleware header as string_view pair (convert from std::string)
        // Use backend_idx to calculate position in middleware_headers
        size_t mid_idx = backend_idx - response->backend_headers.size();
        const auto& [name, value] = response->middleware_headers[mid_idx];
        return {std::string_view(name), std::string_view(value)};
    }
}

Response::AllHeadersIterator& Response::AllHeadersIterator::operator++() {
    // backend_idx tracks total position across both vectors
    ++backend_idx;
    return *this;
}

bool Response::AllHeadersIterator::operator!=(const AllHeadersIterator& other) const {
    // Only compare backend_idx (total position)
    return backend_idx != other.backend_idx;
}

Response::AllHeadersIterator Response::all_headers_begin() const {
    return AllHeadersIterator{this, 0, 0};
}

Response::AllHeadersIterator Response::all_headers_end() const {
    size_t total = backend_headers.size() + middleware_headers.size();
    // backend_idx = total position, middleware_idx unused
    return AllHeadersIterator{this, total, 0};
}

// Conversion functions

std::string_view to_string(Method method) noexcept {
    switch (method) {
        case Method::GET:
            return "GET";
        case Method::POST:
            return "POST";
        case Method::PUT:
            return "PUT";
        case Method::DELETE:
            return "DELETE";
        case Method::HEAD:
            return "HEAD";
        case Method::OPTIONS:
            return "OPTIONS";
        case Method::PATCH:
            return "PATCH";
        case Method::CONNECT:
            return "CONNECT";
        case Method::TRACE:
            return "TRACE";
        case Method::UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

Method parse_method(std::string_view str) noexcept {
    if (str == "GET")
        return Method::GET;
    if (str == "POST")
        return Method::POST;
    if (str == "PUT")
        return Method::PUT;
    if (str == "DELETE")
        return Method::DELETE;
    if (str == "HEAD")
        return Method::HEAD;
    if (str == "OPTIONS")
        return Method::OPTIONS;
    if (str == "PATCH")
        return Method::PATCH;
    if (str == "CONNECT")
        return Method::CONNECT;
    if (str == "TRACE")
        return Method::TRACE;
    return Method::UNKNOWN;
}

std::string_view to_string(Version version) noexcept {
    switch (version) {
        case Version::HTTP_1_0:
            return "HTTP/1.0";
        case Version::HTTP_1_1:
            return "HTTP/1.1";
        case Version::HTTP_2_0:
            return "HTTP/2.0";
        case Version::UNKNOWN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

std::string_view to_reason_phrase(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Continue:
            return "Continue";
        case StatusCode::SwitchingProtocols:
            return "Switching Protocols";
        case StatusCode::OK:
            return "OK";
        case StatusCode::Created:
            return "Created";
        case StatusCode::Accepted:
            return "Accepted";
        case StatusCode::NoContent:
            return "No Content";
        case StatusCode::MovedPermanently:
            return "Moved Permanently";
        case StatusCode::Found:
            return "Found";
        case StatusCode::SeeOther:
            return "See Other";
        case StatusCode::NotModified:
            return "Not Modified";
        case StatusCode::TemporaryRedirect:
            return "Temporary Redirect";
        case StatusCode::PermanentRedirect:
            return "Permanent Redirect";
        case StatusCode::BadRequest:
            return "Bad Request";
        case StatusCode::Unauthorized:
            return "Unauthorized";
        case StatusCode::Forbidden:
            return "Forbidden";
        case StatusCode::NotFound:
            return "Not Found";
        case StatusCode::MethodNotAllowed:
            return "Method Not Allowed";
        case StatusCode::RequestTimeout:
            return "Request Timeout";
        case StatusCode::PayloadTooLarge:
            return "Payload Too Large";
        case StatusCode::URITooLong:
            return "URI Too Long";
        case StatusCode::TooManyRequests:
            return "Too Many Requests";
        case StatusCode::InternalServerError:
            return "Internal Server Error";
        case StatusCode::NotImplemented:
            return "Not Implemented";
        case StatusCode::BadGateway:
            return "Bad Gateway";
        case StatusCode::ServiceUnavailable:
            return "Service Unavailable";
        case StatusCode::GatewayTimeout:
            return "Gateway Timeout";
    }
    return "Unknown";
}

bool header_name_equals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }

    return std::equal(a.begin(), a.end(), b.begin(), [](char ca, char cb) {
        return std::tolower(static_cast<unsigned char>(ca)) ==
               std::tolower(static_cast<unsigned char>(cb));
    });
}

}  // namespace titan::http
