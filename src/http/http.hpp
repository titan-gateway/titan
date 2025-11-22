// Titan HTTP Protocol - Header
// Zero-copy HTTP value types using std::span and std::string_view

#pragma once

#include <cstdint>
#include <span>
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
enum class Version : uint8_t {
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2_0,
    UNKNOWN
};

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
    std::string_view path;        // URI without query string
    std::string_view query;       // Query string (if present)

    // Headers stored in arena (small vector optimization)
    std::vector<Header> headers;

    // Body (view into buffer)
    std::span<const uint8_t> body;

    // Helper: Find header by name (case-insensitive)
    [[nodiscard]] const Header* find_header(std::string_view name) const noexcept;

    // Helper: Get header value or default
    [[nodiscard]] std::string_view get_header(
        std::string_view name,
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

    // Headers (owned or arena-allocated)
    std::vector<Header> headers;

    // Body (may be owned or view)
    std::span<const uint8_t> body;

    // Helper: Find header by name (case-insensitive)
    [[nodiscard]] const Header* find_header(std::string_view name) const noexcept;

    // Helper: Get header value or default
    [[nodiscard]] std::string_view get_header(
        std::string_view name,
        std::string_view default_value = {}) const noexcept;

    // Helper: Check if header exists
    [[nodiscard]] bool has_header(std::string_view name) const noexcept;

    // Helper: Add header
    void add_header(std::string_view name, std::string_view value);

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

} // namespace titan::http
