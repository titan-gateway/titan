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
    // Store value in owned storage to prevent dangling string_view
    // This ensures headers remain valid even when middleware uses temporary strings
    owned_header_values.emplace_back(value);
    headers.push_back({name, owned_header_values.back()});
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
