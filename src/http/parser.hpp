// Titan HTTP Parser - Header
// Zero-copy wrapper around llhttp

#pragma once

#include "http.hpp"
#include "../core/memory.hpp"

#include <llhttp.h>

#include <cstddef>
#include <optional>
#include <span>
#include <system_error>

namespace titan::http {

/// Parse result
enum class ParseResult : uint8_t {
    Complete,      // Request fully parsed
    Incomplete,    // Need more data
    Error          // Parse error
};

/// HTTP/1.1 parser (wraps llhttp)
class Parser {
public:
    Parser();
    ~Parser();

    // Non-copyable, movable
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;

    /// Parse HTTP request from buffer
    /// Returns ParseResult and number of bytes consumed
    /// On Complete, populates 'request' with zero-copy views into 'data'
    [[nodiscard]] std::pair<ParseResult, size_t> parse_request(
        std::span<const uint8_t> data,
        Request& request);

    /// Parse HTTP response from buffer
    /// Returns ParseResult and number of bytes consumed
    /// On Complete, populates 'response' with zero-copy views into 'data'
    [[nodiscard]] std::pair<ParseResult, size_t> parse_response(
        std::span<const uint8_t> data,
        Response& response);

    /// Reset parser state for next request/response
    void reset();

    /// Get last error message
    [[nodiscard]] std::string_view error_message() const noexcept;

    /// Get current parser state
    [[nodiscard]] llhttp_errno_t error_code() const noexcept;

private:
    // llhttp callbacks
    static int on_message_begin(llhttp_t* parser);
    static int on_url(llhttp_t* parser, const char* at, size_t length);
    static int on_status(llhttp_t* parser, const char* at, size_t length);
    static int on_header_field(llhttp_t* parser, const char* at, size_t length);
    static int on_header_value(llhttp_t* parser, const char* at, size_t length);
    static int on_headers_complete(llhttp_t* parser);
    static int on_body(llhttp_t* parser, const char* at, size_t length);
    static int on_message_complete(llhttp_t* parser);

    // Parser state
    llhttp_t parser_;
    llhttp_settings_t settings_;

    // Parsing context (used by callbacks)
    struct Context {
        Request* request = nullptr;
        Response* response = nullptr;
        const uint8_t* buffer_start = nullptr;

        // Temporary storage during parsing
        std::string_view current_header_field;
        bool last_was_field = false;
        bool message_complete = false;
        llhttp_errno_t error = HPE_OK;
    };

    Context ctx_;
    llhttp_type_t parser_type_ = HTTP_REQUEST; // Track current parser type
};

/// Helper: Parse entire HTTP request (convenience wrapper)
/// Returns std::nullopt on error
[[nodiscard]] std::optional<Request> parse_http_request(std::span<const uint8_t> data);

} // namespace titan::http
