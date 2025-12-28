#include "grpc.hpp"

#include <algorithm>
#include <cctype>

namespace titan::http {

std::optional<GrpcMetadata> parse_grpc_path(std::string_view path) {
    // gRPC path format: /package.Service/Method
    // Example: /helloworld.Greeter/SayHello

    // Must start with '/'
    if (path.empty() || path[0] != '/') {
        return std::nullopt;
    }

    // Find the second '/' (separates service from method)
    auto slash_pos = path.find('/', 1);
    if (slash_pos == std::string_view::npos) {
        return std::nullopt;
    }

    // Extract service name (between first and second '/')
    auto service = path.substr(1, slash_pos - 1);
    if (service.empty()) {
        return std::nullopt;
    }

    // Service name must contain at least one '.' (package.Service format)
    if (service.find('.') == std::string_view::npos) {
        return std::nullopt;
    }

    // Extract method name (after second '/')
    auto method = path.substr(slash_pos + 1);
    if (method.empty()) {
        return std::nullopt;
    }

    // Method name should not contain '/'
    if (method.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    GrpcMetadata metadata;
    metadata.service = std::string(service);
    metadata.method = std::string(method);
    metadata.is_grpc_web = false;

    return metadata;
}

bool is_grpc_request(std::string_view content_type) {
    // gRPC Content-Type must start with "application/grpc"
    // Valid formats:
    // - application/grpc
    // - application/grpc+proto
    // - application/grpc+json
    // - application/grpc-web
    // - application/grpc-web+proto
    // - application/grpc-web-text
    // - application/grpc-web-text+proto

    if (content_type.empty()) {
        return false;
    }

    // Check for "application/grpc" prefix (case-insensitive)
    constexpr std::string_view grpc_prefix = "application/grpc";

    if (content_type.size() < grpc_prefix.size()) {
        return false;
    }

    // Case-insensitive comparison for prefix
    auto prefix = content_type.substr(0, grpc_prefix.size());
    return std::equal(
        prefix.begin(), prefix.end(),
        grpc_prefix.begin(), grpc_prefix.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); }
    );
}

bool is_grpc_web_request(std::string_view content_type) {
    // gRPC-Web Content-Types:
    // - application/grpc-web
    // - application/grpc-web+proto
    // - application/grpc-web-text
    // - application/grpc-web-text+proto

    if (content_type.empty()) {
        return false;
    }

    // Check for "application/grpc-web" prefix (case-insensitive)
    constexpr std::string_view grpc_web_prefix = "application/grpc-web";

    if (content_type.size() < grpc_web_prefix.size()) {
        return false;
    }

    // Case-insensitive comparison
    auto prefix = content_type.substr(0, grpc_web_prefix.size());
    return std::equal(
        prefix.begin(), prefix.end(),
        grpc_web_prefix.begin(), grpc_web_prefix.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); }
    );
}

std::optional<uint32_t> extract_grpc_status(
    const std::vector<std::pair<std::string, std::string>>& trailers
) {
    for (const auto& [name, value] : trailers) {
        if (name == "grpc-status") {
            try {
                return static_cast<uint32_t>(std::stoul(value));
            } catch (const std::exception&) {
                // Invalid grpc-status value, return nullopt
                return std::nullopt;
            }
        }
    }
    return std::nullopt;
}

std::string extract_grpc_message(
    const std::vector<std::pair<std::string, std::string>>& trailers
) {
    for (const auto& [name, value] : trailers) {
        if (name == "grpc-message") {
            return value;
        }
    }
    return "";
}

int grpc_status_to_http(uint32_t grpc_status) {
    // Mapping based on gRPC HTTP/2 spec:
    // https://github.com/grpc/grpc/blob/master/doc/http-grpc-status-mapping.md

    switch (static_cast<GrpcStatus>(grpc_status)) {
        case GrpcStatus::OK:
            return 200;  // OK
        case GrpcStatus::CANCELLED:
            return 499;  // Client Closed Request (non-standard but widely used)
        case GrpcStatus::INVALID_ARGUMENT:
            return 400;  // Bad Request
        case GrpcStatus::DEADLINE_EXCEEDED:
            return 504;  // Gateway Timeout
        case GrpcStatus::NOT_FOUND:
            return 404;  // Not Found
        case GrpcStatus::ALREADY_EXISTS:
            return 409;  // Conflict
        case GrpcStatus::PERMISSION_DENIED:
            return 403;  // Forbidden
        case GrpcStatus::RESOURCE_EXHAUSTED:
            return 429;  // Too Many Requests
        case GrpcStatus::FAILED_PRECONDITION:
            return 400;  // Bad Request
        case GrpcStatus::ABORTED:
            return 409;  // Conflict
        case GrpcStatus::OUT_OF_RANGE:
            return 400;  // Bad Request
        case GrpcStatus::UNIMPLEMENTED:
            return 501;  // Not Implemented
        case GrpcStatus::INTERNAL:
            return 500;  // Internal Server Error
        case GrpcStatus::UNAVAILABLE:
            return 503;  // Service Unavailable
        case GrpcStatus::DATA_LOSS:
            return 500;  // Internal Server Error
        case GrpcStatus::UNAUTHENTICATED:
            return 401;  // Unauthorized
        default:
            return 500;  // Internal Server Error (unknown status)
    }
}

std::string_view grpc_status_name(uint32_t grpc_status) {
    switch (static_cast<GrpcStatus>(grpc_status)) {
        case GrpcStatus::OK:
            return "OK";
        case GrpcStatus::CANCELLED:
            return "CANCELLED";
        case GrpcStatus::UNKNOWN:
            return "UNKNOWN";
        case GrpcStatus::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case GrpcStatus::DEADLINE_EXCEEDED:
            return "DEADLINE_EXCEEDED";
        case GrpcStatus::NOT_FOUND:
            return "NOT_FOUND";
        case GrpcStatus::ALREADY_EXISTS:
            return "ALREADY_EXISTS";
        case GrpcStatus::PERMISSION_DENIED:
            return "PERMISSION_DENIED";
        case GrpcStatus::RESOURCE_EXHAUSTED:
            return "RESOURCE_EXHAUSTED";
        case GrpcStatus::FAILED_PRECONDITION:
            return "FAILED_PRECONDITION";
        case GrpcStatus::ABORTED:
            return "ABORTED";
        case GrpcStatus::OUT_OF_RANGE:
            return "OUT_OF_RANGE";
        case GrpcStatus::UNIMPLEMENTED:
            return "UNIMPLEMENTED";
        case GrpcStatus::INTERNAL:
            return "INTERNAL";
        case GrpcStatus::UNAVAILABLE:
            return "UNAVAILABLE";
        case GrpcStatus::DATA_LOSS:
            return "DATA_LOSS";
        case GrpcStatus::UNAUTHENTICATED:
            return "UNAUTHENTICATED";
        default:
            return "UNKNOWN";
    }
}

}  // namespace titan::http
