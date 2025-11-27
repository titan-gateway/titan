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


// Titan HTTP Parser - Implementation

#include "parser.hpp"

#include <cstring>

namespace titan::http {

Parser::Parser() {
    // Initialize llhttp settings
    llhttp_settings_init(&settings_);

    // Register callbacks
    settings_.on_message_begin = on_message_begin;
    settings_.on_url = on_url;
    settings_.on_status = on_status;
    settings_.on_header_field = on_header_field;
    settings_.on_header_value = on_header_value;
    settings_.on_headers_complete = on_headers_complete;
    settings_.on_body = on_body;
    settings_.on_message_complete = on_message_complete;

    // Initialize parser
    llhttp_init(&parser_, HTTP_REQUEST, &settings_);
    parser_.data = &ctx_;
}

Parser::~Parser() = default;

Parser::Parser(Parser&& other) noexcept
    : parser_(other.parser_)
    , settings_(other.settings_)
    , ctx_(other.ctx_) {
    parser_.data = &ctx_;
}

Parser& Parser::operator=(Parser&& other) noexcept {
    if (this != &other) {
        parser_ = other.parser_;
        settings_ = other.settings_;
        ctx_ = other.ctx_;
        parser_.data = &ctx_;
    }
    return *this;
}

std::pair<ParseResult, size_t> Parser::parse_request(
    std::span<const uint8_t> data,
    Request& request) {

    // Initialize parser for request if needed
    if (parser_type_ != HTTP_REQUEST) {
        llhttp_init(&parser_, HTTP_REQUEST, &settings_);
        parser_.data = &ctx_;
        parser_type_ = HTTP_REQUEST;
    }

    // Set up context
    ctx_.request = &request;
    ctx_.response = nullptr;
    ctx_.buffer_start = data.data();
    ctx_.message_complete = false;
    ctx_.error = HPE_OK;

    // Execute parser
    llhttp_errno_t err = llhttp_execute(
        &parser_,
        reinterpret_cast<const char*>(data.data()),
        data.size());

    // Calculate bytes consumed
    size_t consumed = data.size();

    // On error, get actual error position
    if (err != HPE_OK && err != HPE_PAUSED_UPGRADE) {
        const char* error_pos = llhttp_get_error_pos(&parser_);
        if (error_pos) {
            consumed = static_cast<size_t>(
                reinterpret_cast<const uint8_t*>(error_pos) - data.data());
        }
        ctx_.error = err;
        return {ParseResult::Error, consumed};
    }

    // Check if message is complete
    if (ctx_.message_complete) {
        return {ParseResult::Complete, consumed};
    }

    // Need more data (incomplete request)
    return {ParseResult::Incomplete, consumed};
}

std::pair<ParseResult, size_t> Parser::parse_response(
    std::span<const uint8_t> data,
    Response& response) {

    // Initialize parser for response if needed
    if (parser_type_ != HTTP_RESPONSE) {
        llhttp_init(&parser_, HTTP_RESPONSE, &settings_);
        parser_.data = &ctx_;
        parser_type_ = HTTP_RESPONSE;
    }

    // Set up context
    ctx_.request = nullptr;
    ctx_.response = &response;
    ctx_.buffer_start = data.data();
    ctx_.message_complete = false;
    ctx_.error = HPE_OK;

    // Execute parser
    llhttp_errno_t err = llhttp_execute(
        &parser_,
        reinterpret_cast<const char*>(data.data()),
        data.size());

    // Calculate bytes consumed
    size_t consumed = data.size();

    // On error, get actual error position
    if (err != HPE_OK && err != HPE_PAUSED_UPGRADE) {
        const char* error_pos = llhttp_get_error_pos(&parser_);
        if (error_pos) {
            consumed = static_cast<size_t>(
                reinterpret_cast<const uint8_t*>(error_pos) - data.data());
        }
        ctx_.error = err;
        return {ParseResult::Error, consumed};
    }

    // Check if message is complete
    if (ctx_.message_complete) {
        return {ParseResult::Complete, consumed};
    }

    // Need more data (incomplete response)
    return {ParseResult::Incomplete, consumed};
}

void Parser::reset() {
    llhttp_init(&parser_, parser_type_, &settings_);
    parser_.data = &ctx_;
    ctx_ = Context{};
}

std::string_view Parser::error_message() const noexcept {
    if (ctx_.error == HPE_OK) {
        return "";
    }
    return llhttp_errno_name(ctx_.error);
}

llhttp_errno_t Parser::error_code() const noexcept {
    return ctx_.error;
}

// Callbacks

int Parser::on_message_begin(llhttp_t* parser) {
    auto* ctx = static_cast<Context*>(parser->data);
    ctx->message_complete = false;
    ctx->error = HPE_OK;
    return 0;
}

int Parser::on_url(llhttp_t* parser, const char* at, size_t length) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->request) return -1;

    // Store URI as view into buffer
    ctx->request->uri = std::string_view(at, length);

    // Split path and query
    size_t query_pos = ctx->request->uri.find('?');
    if (query_pos != std::string_view::npos) {
        ctx->request->path = ctx->request->uri.substr(0, query_pos);
        ctx->request->query = ctx->request->uri.substr(query_pos + 1);
    } else {
        ctx->request->path = ctx->request->uri;
        ctx->request->query = {};
    }

    return 0;
}

int Parser::on_status(llhttp_t* parser, const char* at, size_t length) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->response) return 0; // Only used for response parsing

    // Store status reason phrase (e.g., "OK", "Not Found")
    // We don't actually use this for now, just validate
    (void)at;
    (void)length;
    return 0;
}

int Parser::on_header_field(llhttp_t* parser, const char* at, size_t length) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->request && !ctx->response) return -1;

    // If previous was a value, we're starting a new header
    if (!ctx->last_was_field) {
        ctx->current_header_field = std::string_view(at, length);
    } else {
        // Continuation of previous field (shouldn't happen normally)
        // For simplicity, replace with new field
        ctx->current_header_field = std::string_view(at, length);
    }

    ctx->last_was_field = true;
    return 0;
}

int Parser::on_header_value(llhttp_t* parser, const char* at, size_t length) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->request && !ctx->response) return -1;

    // Add header to request or response
    Header header{ctx->current_header_field, std::string_view(at, length)};

    if (ctx->request) {
        ctx->request->headers.push_back(header);
    } else if (ctx->response) {
        ctx->response->headers.push_back(header);
    }

    ctx->last_was_field = false;
    return 0;
}

int Parser::on_headers_complete(llhttp_t* parser) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->request && !ctx->response) return -1;

    // Extract version (common to both request and response)
    uint8_t major = parser->http_major;
    uint8_t minor = parser->http_minor;
    Version version = Version::UNKNOWN;
    if (major == 1 && minor == 0) {
        version = Version::HTTP_1_0;
    } else if (major == 1 && minor == 1) {
        version = Version::HTTP_1_1;
    } else if (major == 2 && minor == 0) {
        version = Version::HTTP_2_0;
    }

    if (ctx->request) {
        // Extract method for requests
        uint8_t method = llhttp_get_method(parser);
        switch (method) {
            case HTTP_GET: ctx->request->method = Method::GET; break;
            case HTTP_POST: ctx->request->method = Method::POST; break;
            case HTTP_PUT: ctx->request->method = Method::PUT; break;
            case HTTP_DELETE: ctx->request->method = Method::DELETE; break;
            case HTTP_HEAD: ctx->request->method = Method::HEAD; break;
            case HTTP_OPTIONS: ctx->request->method = Method::OPTIONS; break;
            case HTTP_PATCH: ctx->request->method = Method::PATCH; break;
            case HTTP_CONNECT: ctx->request->method = Method::CONNECT; break;
            case HTTP_TRACE: ctx->request->method = Method::TRACE; break;
            default: ctx->request->method = Method::UNKNOWN; break;
        }
        ctx->request->version = version;
    } else if (ctx->response) {
        // Extract status code for responses
        uint16_t status = parser->status_code;
        ctx->response->status = static_cast<StatusCode>(status);
        ctx->response->version = version;
    }

    return 0;
}

int Parser::on_body(llhttp_t* parser, const char* at, size_t length) {
    auto* ctx = static_cast<Context*>(parser->data);
    if (!ctx->request && !ctx->response) return -1;

    // Store body as span (zero-copy view into buffer)
    const uint8_t* body_start = reinterpret_cast<const uint8_t*>(at);
    std::span<const uint8_t> body_span(body_start, length);

    if (ctx->request) {
        ctx->request->body = body_span;
    } else if (ctx->response) {
        // For responses, store as span
        // The caller must ensure the buffer stays alive
        ctx->response->body = body_span;
    }

    return 0;
}

int Parser::on_message_complete(llhttp_t* parser) {
    auto* ctx = static_cast<Context*>(parser->data);
    ctx->message_complete = true;
    return 0;
}

// Convenience wrapper

std::optional<Request> parse_http_request(std::span<const uint8_t> data) {
    Parser parser;
    Request request;

    auto [result, consumed] = parser.parse_request(data, request);

    if (result == ParseResult::Complete) {
        return request;
    }

    return std::nullopt;
}

} // namespace titan::http
