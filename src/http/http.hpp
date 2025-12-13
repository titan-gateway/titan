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

// Titan HTTP Protocol - Header
// Zero-copy HTTP value types using std::span and std::string_view

#pragma once

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace titan::http {

/// HTTP methods
enum class Method : uint8_t {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    CONNECT,
    TRACE,
    UNKNOWN
};

/// HTTP version
enum class Version : uint8_t { HTTP_1_0, HTTP_1_1, HTTP_2_0, UNKNOWN };

/// HTTP status codes
enum class StatusCode : uint16_t {
    // 1xx Informational
    Continue = 100,
    SwitchingProtocols = 101,

    // 2xx Success
    OK = 200,
    Created = 201,
    Accepted = 202,
    NoContent = 204,

    // 3xx Redirection
    MovedPermanently = 301,
    Found = 302,
    SeeOther = 303,
    NotModified = 304,
    TemporaryRedirect = 307,
    PermanentRedirect = 308,

    // 4xx Client Error
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    RequestTimeout = 408,
    PayloadTooLarge = 413,
    URITooLong = 414,
    TooManyRequests = 429,

    // 5xx Server Error
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504,
};

/// HTTP header (name-value pair)
/// Both name and value are views into the request buffer (zero-copy)
struct Header {
    std::string_view name;
    std::string_view value;
};

/// HTTP request (zero-copy, all views into buffer)
struct Request {
    Method method = Method::UNKNOWN;
    Version version = Version::HTTP_1_1;

    // Zero-copy views into request buffer
    std::string_view uri;
    std::string_view path;   // URI without query string
    std::string_view query;  // Query string (if present)

    // Headers stored in arena (small vector optimization)
    std::vector<Header> headers;

    // Body (view into buffer)
    std::span<const uint8_t> body;

    // Helper: Find header by name (case-insensitive)
    [[nodiscard]] const Header* find_header(std::string_view name) const noexcept;

    // Helper: Get header value or default
    [[nodiscard]] std::string_view get_header(std::string_view name,
                                              std::string_view default_value = {}) const noexcept;

    // Helper: Check if header exists
    [[nodiscard]] bool has_header(std::string_view name) const noexcept;

    // Content-Length helper
    [[nodiscard]] size_t content_length() const noexcept;

    // Connection: keep-alive helper
    [[nodiscard]] bool keep_alive() const noexcept;
};

/// HTTP response
struct Response {
    Version version = Version::HTTP_1_1;
    StatusCode status = StatusCode::OK;
    std::string_view reason_phrase;

    // HYBRID STORAGE MODEL (Phase 2 - New Design):
    // Separates backend headers (zero-copy views) from middleware headers (owned strings)
    // This eliminates stack-use-after-return bugs and reallocation corruption

    // Backend headers: Zero-copy string_views into recv_buffer or response_header_storage
    // Lifetime: Valid until recv_buffer is reused (short-lived, performance-critical)
    // Usage: Populated by parser, copied from upstream responses
    std::vector<Header> backend_headers;

    // Middleware headers: Owned std::string pairs (safe, long-lived)
    // Lifetime: Valid until Response is destroyed (owned by Response)
    // Usage: Added by middleware (ProxyMiddleware, CORSMiddleware, etc.)
    // Allocations: Acceptable overhead (typically 2 headers = 4 allocations per request)
    std::vector<std::pair<std::string, std::string>> middleware_headers;

    // DEPRECATED (Phase 1 compatibility):
    // Old unified storage - will be removed after full migration
    std::vector<Header> headers;
    std::string header_storage;

    // Reserve capacity on first use (lazy initialization)
    // DEPRECATED: Only used by old add_header() path
    inline void ensure_header_storage_capacity() {
        if (header_storage.empty()) {
            header_storage.reserve(2048);  // 2KB should cover most responses
        }
    }

    // Body storage for owned data (e.g., compressed responses)
    std::vector<uint8_t> body_storage;

    // Body (may be owned or view)
    std::span<const uint8_t> body;

    // Helper: Find header by name (case-insensitive)
    [[nodiscard]] const Header* find_header(std::string_view name) const noexcept;

    // Helper: Get header value or default
    [[nodiscard]] std::string_view get_header(std::string_view name,
                                              std::string_view default_value = {}) const noexcept;

    // Helper: Check if header exists
    [[nodiscard]] bool has_header(std::string_view name) const noexcept;

    // HYBRID STORAGE API (New - Phase 2):

    // Add backend header (zero-copy, string_view into external storage)
    // Use this for headers from parsed backend responses
    // CRITICAL: Caller must ensure string_view lifetime (typically points to recv_buffer)
    void add_backend_header(std::string_view name, std::string_view value);

    // Add middleware header (safe, owned strings)
    // Use this for headers generated by middleware
    // SAFE: Always copies data, no lifetime concerns, cannot create dangling pointers
    void add_middleware_header(std::string_view name, std::string_view value);

    // Iterator over all headers (backend + middleware)
    // Returns pairs of string_views for uniform access
    struct AllHeadersIterator {
        const Response* response;
        size_t backend_idx = 0;
        size_t middleware_idx = 0;

        std::pair<std::string_view, std::string_view> operator*() const;
        AllHeadersIterator& operator++();
        bool operator!=(const AllHeadersIterator& other) const;
    };

    AllHeadersIterator all_headers_begin() const;
    AllHeadersIterator all_headers_end() const;

    // DEPRECATED (Phase 1 compatibility):
    // Old unified API - will be removed after migration

    // Helper: Add header (DEPRECATED - use add_middleware_header or add_backend_header)
    void add_header(std::string_view name, std::string_view value);

    // Helper: Remove header by name (case-insensitive)
    // Returns true if header was found and removed
    bool remove_header(std::string_view name);

    // Helper: Modify existing header value (case-insensitive)
    // Returns true if header was found and modified
    // If header doesn't exist, this is a no-op (returns false)
    bool modify_header(std::string_view name, std::string_view new_value);

    // Helper: Set content length
    void set_content_length(size_t length);

    // Helper: Set content type
    void set_content_type(std::string_view content_type);

    // Content-Length helper
    [[nodiscard]] size_t content_length() const noexcept;

    // Connection: keep-alive helper
    [[nodiscard]] bool keep_alive() const noexcept;
};

// Conversion functions

/// Convert Method to string
[[nodiscard]] std::string_view to_string(Method method) noexcept;

/// Convert string to Method
[[nodiscard]] Method parse_method(std::string_view str) noexcept;

/// Convert Version to string
[[nodiscard]] std::string_view to_string(Version version) noexcept;

/// Convert StatusCode to reason phrase
[[nodiscard]] std::string_view to_reason_phrase(StatusCode code) noexcept;

/// Case-insensitive header name comparison
[[nodiscard]] bool header_name_equals(std::string_view a, std::string_view b) noexcept;

}  // namespace titan::http
