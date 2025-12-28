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

// Titan gRPC Unit Tests

#include <catch2/catch_test_macros.hpp>

#include "http/grpc.hpp"

using namespace titan::http;

// ============================
// parse_grpc_path Tests
// ============================

TEST_CASE("parse_grpc_path - Valid paths", "[grpc]") {
    SECTION("Simple service.method") {
        auto result = parse_grpc_path("/helloworld.Greeter/SayHello");
        REQUIRE(result.has_value());
        REQUIRE(result->service == "helloworld.Greeter");
        REQUIRE(result->method == "SayHello");
        REQUIRE_FALSE(result->is_grpc_web);
    }

    SECTION("Multi-level package") {
        auto result = parse_grpc_path("/api.v1.UserService/GetUser");
        REQUIRE(result.has_value());
        REQUIRE(result->service == "api.v1.UserService");
        REQUIRE(result->method == "GetUser");
    }

    SECTION("Long service name") {
        auto result = parse_grpc_path("/com.example.microservice.v2.AuthService/ValidateToken");
        REQUIRE(result.has_value());
        REQUIRE(result->service == "com.example.microservice.v2.AuthService");
        REQUIRE(result->method == "ValidateToken");
    }

    SECTION("CamelCase method") {
        auto result = parse_grpc_path("/routeguide.RouteGuide/GetFeature");
        REQUIRE(result.has_value());
        REQUIRE(result->service == "routeguide.RouteGuide");
        REQUIRE(result->method == "GetFeature");
    }
}

TEST_CASE("parse_grpc_path - Invalid paths", "[grpc]") {
    SECTION("Empty path") {
        auto result = parse_grpc_path("");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("No leading slash") {
        auto result = parse_grpc_path("helloworld.Greeter/SayHello");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Missing method separator") {
        auto result = parse_grpc_path("/helloworld.Greeter");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("No package (missing dot)") {
        auto result = parse_grpc_path("/Greeter/SayHello");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty service name") {
        auto result = parse_grpc_path("//SayHello");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Empty method name") {
        auto result = parse_grpc_path("/helloworld.Greeter/");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Multiple slashes in method") {
        auto result = parse_grpc_path("/helloworld.Greeter/Say/Hello");
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("HTTP path (not gRPC)") {
        auto result = parse_grpc_path("/api/users/123");
        REQUIRE_FALSE(result.has_value());
    }
}

// ============================
// is_grpc_request Tests
// ============================

TEST_CASE("is_grpc_request - Valid gRPC Content-Types", "[grpc]") {
    SECTION("application/grpc") {
        REQUIRE(is_grpc_request("application/grpc"));
    }

    SECTION("application/grpc+proto") {
        REQUIRE(is_grpc_request("application/grpc+proto"));
    }

    SECTION("application/grpc+json") {
        REQUIRE(is_grpc_request("application/grpc+json"));
    }

    SECTION("application/grpc-web") {
        REQUIRE(is_grpc_request("application/grpc-web"));
    }

    SECTION("application/grpc-web+proto") {
        REQUIRE(is_grpc_request("application/grpc-web+proto"));
    }

    SECTION("application/grpc-web-text") {
        REQUIRE(is_grpc_request("application/grpc-web-text"));
    }

    SECTION("application/grpc-web-text+proto") {
        REQUIRE(is_grpc_request("application/grpc-web-text+proto"));
    }

    SECTION("Case insensitive - APPLICATION/GRPC") {
        REQUIRE(is_grpc_request("APPLICATION/GRPC"));
    }

    SECTION("Case insensitive - Application/Grpc+Proto") {
        REQUIRE(is_grpc_request("Application/Grpc+Proto"));
    }
}

TEST_CASE("is_grpc_request - Non-gRPC Content-Types", "[grpc]") {
    SECTION("Empty string") {
        REQUIRE_FALSE(is_grpc_request(""));
    }

    SECTION("application/json") {
        REQUIRE_FALSE(is_grpc_request("application/json"));
    }

    SECTION("text/html") {
        REQUIRE_FALSE(is_grpc_request("text/html"));
    }

    SECTION("application/octet-stream") {
        REQUIRE_FALSE(is_grpc_request("application/octet-stream"));
    }

    SECTION("Partial match - application/gr") {
        REQUIRE_FALSE(is_grpc_request("application/gr"));
    }
}

// ============================
// is_grpc_web_request Tests
// ============================

TEST_CASE("is_grpc_web_request - Valid gRPC-Web Content-Types", "[grpc]") {
    SECTION("application/grpc-web") {
        REQUIRE(is_grpc_web_request("application/grpc-web"));
    }

    SECTION("application/grpc-web+proto") {
        REQUIRE(is_grpc_web_request("application/grpc-web+proto"));
    }

    SECTION("application/grpc-web-text") {
        REQUIRE(is_grpc_web_request("application/grpc-web-text"));
    }

    SECTION("application/grpc-web-text+proto") {
        REQUIRE(is_grpc_web_request("application/grpc-web-text+proto"));
    }

    SECTION("Case insensitive") {
        REQUIRE(is_grpc_web_request("Application/GRPC-WEB+PROTO"));
    }
}

TEST_CASE("is_grpc_web_request - Non-gRPC-Web Content-Types", "[grpc]") {
    SECTION("Standard gRPC (not gRPC-Web)") {
        REQUIRE_FALSE(is_grpc_web_request("application/grpc"));
    }

    SECTION("Standard gRPC+proto (not gRPC-Web)") {
        REQUIRE_FALSE(is_grpc_web_request("application/grpc+proto"));
    }

    SECTION("Empty string") {
        REQUIRE_FALSE(is_grpc_web_request(""));
    }

    SECTION("application/json") {
        REQUIRE_FALSE(is_grpc_web_request("application/json"));
    }
}

// ============================
// extract_grpc_status Tests
// ============================

TEST_CASE("extract_grpc_status - Valid status codes", "[grpc]") {
    SECTION("OK status (0)") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "0"}, {"grpc-message", "OK"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 0);
    }

    SECTION("CANCELLED status (1)") {
        std::vector<std::pair<std::string, std::string>> trailers = {{"grpc-status", "1"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 1);
    }

    SECTION("INVALID_ARGUMENT status (3)") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "3"}, {"grpc-message", "Invalid argument"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 3);
    }

    SECTION("UNAUTHENTICATED status (16)") {
        std::vector<std::pair<std::string, std::string>> trailers = {{"grpc-status", "16"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 16);
    }

    SECTION("Multiple trailers, grpc-status present") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"content-length", "123"},
            {"grpc-status", "5"},
            {"grpc-message", "Not found"},
            {"x-custom-header", "value"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 5);
    }
}

TEST_CASE("extract_grpc_status - Missing or invalid status", "[grpc]") {
    SECTION("Empty trailers") {
        std::vector<std::pair<std::string, std::string>> trailers;
        auto status = extract_grpc_status(trailers);
        REQUIRE_FALSE(status.has_value());
    }

    SECTION("No grpc-status header") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-message", "Some message"}, {"content-length", "456"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE_FALSE(status.has_value());
    }

    SECTION("Invalid grpc-status value (non-numeric)") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "invalid"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE_FALSE(status.has_value());
    }

    SECTION("Invalid grpc-status value (empty)") {
        std::vector<std::pair<std::string, std::string>> trailers = {{"grpc-status", ""}};
        auto status = extract_grpc_status(trailers);
        REQUIRE_FALSE(status.has_value());
    }
}

// ============================
// extract_grpc_message Tests
// ============================

TEST_CASE("extract_grpc_message", "[grpc]") {
    SECTION("Valid grpc-message") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "0"}, {"grpc-message", "Operation successful"}};
        auto message = extract_grpc_message(trailers);
        REQUIRE(message == "Operation successful");
    }

    SECTION("Empty grpc-message") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "0"}, {"grpc-message", ""}};
        auto message = extract_grpc_message(trailers);
        REQUIRE(message.empty());
    }

    SECTION("No grpc-message header") {
        std::vector<std::pair<std::string, std::string>> trailers = {{"grpc-status", "0"}};
        auto message = extract_grpc_message(trailers);
        REQUIRE(message.empty());
    }

    SECTION("Error message") {
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "13"}, {"grpc-message", "Internal server error occurred"}};
        auto message = extract_grpc_message(trailers);
        REQUIRE(message == "Internal server error occurred");
    }
}

// ============================
// grpc_status_to_http Tests
// ============================

TEST_CASE("grpc_status_to_http - Status code mapping", "[grpc]") {
    SECTION("OK → 200") {
        REQUIRE(grpc_status_to_http(0) == 200);
    }

    SECTION("CANCELLED → 499") {
        REQUIRE(grpc_status_to_http(1) == 499);
    }

    SECTION("INVALID_ARGUMENT → 400") {
        REQUIRE(grpc_status_to_http(3) == 400);
    }

    SECTION("DEADLINE_EXCEEDED → 504") {
        REQUIRE(grpc_status_to_http(4) == 504);
    }

    SECTION("NOT_FOUND → 404") {
        REQUIRE(grpc_status_to_http(5) == 404);
    }

    SECTION("ALREADY_EXISTS → 409") {
        REQUIRE(grpc_status_to_http(6) == 409);
    }

    SECTION("PERMISSION_DENIED → 403") {
        REQUIRE(grpc_status_to_http(7) == 403);
    }

    SECTION("RESOURCE_EXHAUSTED → 429") {
        REQUIRE(grpc_status_to_http(8) == 429);
    }

    SECTION("UNIMPLEMENTED → 501") {
        REQUIRE(grpc_status_to_http(12) == 501);
    }

    SECTION("INTERNAL → 500") {
        REQUIRE(grpc_status_to_http(13) == 500);
    }

    SECTION("UNAVAILABLE → 503") {
        REQUIRE(grpc_status_to_http(14) == 503);
    }

    SECTION("UNAUTHENTICATED → 401") {
        REQUIRE(grpc_status_to_http(16) == 401);
    }

    SECTION("Unknown status → 500") {
        REQUIRE(grpc_status_to_http(99) == 500);
        REQUIRE(grpc_status_to_http(255) == 500);
    }
}

// ============================
// grpc_status_name Tests
// ============================

TEST_CASE("grpc_status_name - Status name mapping", "[grpc]") {
    SECTION("OK") {
        REQUIRE(grpc_status_name(0) == "OK");
    }

    SECTION("CANCELLED") {
        REQUIRE(grpc_status_name(1) == "CANCELLED");
    }

    SECTION("UNKNOWN") {
        REQUIRE(grpc_status_name(2) == "UNKNOWN");
    }

    SECTION("INVALID_ARGUMENT") {
        REQUIRE(grpc_status_name(3) == "INVALID_ARGUMENT");
    }

    SECTION("NOT_FOUND") {
        REQUIRE(grpc_status_name(5) == "NOT_FOUND");
    }

    SECTION("PERMISSION_DENIED") {
        REQUIRE(grpc_status_name(7) == "PERMISSION_DENIED");
    }

    SECTION("RESOURCE_EXHAUSTED") {
        REQUIRE(grpc_status_name(8) == "RESOURCE_EXHAUSTED");
    }

    SECTION("UNIMPLEMENTED") {
        REQUIRE(grpc_status_name(12) == "UNIMPLEMENTED");
    }

    SECTION("INTERNAL") {
        REQUIRE(grpc_status_name(13) == "INTERNAL");
    }

    SECTION("UNAVAILABLE") {
        REQUIRE(grpc_status_name(14) == "UNAVAILABLE");
    }

    SECTION("UNAUTHENTICATED") {
        REQUIRE(grpc_status_name(16) == "UNAUTHENTICATED");
    }

    SECTION("Unknown status code") {
        REQUIRE(grpc_status_name(99) == "UNKNOWN");
        REQUIRE(grpc_status_name(255) == "UNKNOWN");
    }
}

// ============================
// Integration Tests
// ============================

TEST_CASE("gRPC path parsing and status extraction integration", "[grpc]") {
    SECTION("Full gRPC request flow") {
        // Parse path
        auto path_result = parse_grpc_path("/helloworld.Greeter/SayHello");
        REQUIRE(path_result.has_value());
        REQUIRE(path_result->service == "helloworld.Greeter");
        REQUIRE(path_result->method == "SayHello");

        // Check content type
        REQUIRE(is_grpc_request("application/grpc+proto"));
        REQUIRE_FALSE(is_grpc_web_request("application/grpc+proto"));

        // Extract status from trailers
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "0"}, {"grpc-message", "Success"}};
        auto status = extract_grpc_status(trailers);
        REQUIRE(status.has_value());
        REQUIRE(status.value() == 0);

        // Convert to HTTP status
        REQUIRE(grpc_status_to_http(status.value()) == 200);
        REQUIRE(grpc_status_name(status.value()) == "OK");
    }

    SECTION("gRPC-Web request flow") {
        // Parse path
        auto path_result = parse_grpc_path("/api.v1.Service/Method");
        REQUIRE(path_result.has_value());

        // Check content type
        REQUIRE(is_grpc_request("application/grpc-web+proto"));
        REQUIRE(is_grpc_web_request("application/grpc-web+proto"));

        // Mark as gRPC-Web
        path_result->is_grpc_web = true;
        REQUIRE(path_result->is_grpc_web);
    }

    SECTION("Error response flow") {
        // Parse path
        auto path_result = parse_grpc_path("/service.API/FailingMethod");
        REQUIRE(path_result.has_value());

        // Extract error status
        std::vector<std::pair<std::string, std::string>> trailers = {
            {"grpc-status", "13"}, {"grpc-message", "Internal error"}};
        auto status = extract_grpc_status(trailers);
        auto message = extract_grpc_message(trailers);

        REQUIRE(status.has_value());
        REQUIRE(status.value() == 13);
        REQUIRE(message == "Internal error");
        REQUIRE(grpc_status_to_http(status.value()) == 500);
        REQUIRE(grpc_status_name(status.value()) == "INTERNAL");
    }
}
