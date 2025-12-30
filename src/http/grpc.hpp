#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace titan::http {

/**
 * @brief gRPC metadata extracted from request path
 *
 * gRPC uses paths in the format: /package.Service/Method
 * Example: /helloworld.Greeter/SayHello
 *   - service: "helloworld.Greeter"
 *   - method: "SayHello"
 */
struct GrpcMetadata {
    std::string service;  // package.Service (e.g., "helloworld.Greeter")
    std::string method;   // Method name (e.g., "SayHello")
    bool is_grpc_web = false;  // true if Content-Type is application/grpc-web*
};

/**
 * @brief gRPC status codes (subset of common codes)
 *
 * Full spec: https://grpc.github.io/grpc/core/md_doc_statuscodes.html
 */
enum class GrpcStatus : uint32_t {
    OK = 0,
    CANCELLED = 1,
    UNKNOWN = 2,
    INVALID_ARGUMENT = 3,
    DEADLINE_EXCEEDED = 4,
    NOT_FOUND = 5,
    ALREADY_EXISTS = 6,
    PERMISSION_DENIED = 7,
    RESOURCE_EXHAUSTED = 8,
    FAILED_PRECONDITION = 9,
    ABORTED = 10,
    OUT_OF_RANGE = 11,
    UNIMPLEMENTED = 12,
    INTERNAL = 13,
    UNAVAILABLE = 14,
    DATA_LOSS = 15,
    UNAUTHENTICATED = 16,
};

/**
 * @brief Parse gRPC method path into service and method components
 *
 * @param path HTTP/2 :path pseudo-header (e.g., "/helloworld.Greeter/SayHello")
 * @return GrpcMetadata if path is valid gRPC format, std::nullopt otherwise
 *
 * Valid gRPC path format:
 * - Must start with '/'
 * - Must contain exactly one '/' after the initial '/'
 * - Service name must contain at least one '.' (package.Service)
 * - Method name must not be empty
 *
 * Examples:
 *   "/helloworld.Greeter/SayHello" → {service: "helloworld.Greeter", method: "SayHello"}
 *   "/api.v1.UserService/GetUser" → {service: "api.v1.UserService", method: "GetUser"}
 *   "/invalid" → std::nullopt
 */
std::optional<GrpcMetadata> parse_grpc_path(std::string_view path);

/**
 * @brief Check if Content-Type header indicates a gRPC request
 *
 * @param content_type Value of Content-Type header
 * @return true if this is a gRPC or gRPC-Web request
 *
 * Valid gRPC Content-Types:
 * - application/grpc
 * - application/grpc+proto (protobuf encoding)
 * - application/grpc+json (JSON encoding, rare)
 * - application/grpc-web (browser clients)
 * - application/grpc-web+proto
 * - application/grpc-web-text (base64-encoded)
 * - application/grpc-web-text+proto
 *
 * Note: gRPC spec mandates servers return 415 Unsupported Media Type
 * if Content-Type doesn't match application/grpc*
 */
bool is_grpc_request(std::string_view content_type);

/**
 * @brief Check if Content-Type indicates gRPC-Web (browser client)
 *
 * @param content_type Value of Content-Type header
 * @return true if this is a gRPC-Web request (requires special handling)
 */
bool is_grpc_web_request(std::string_view content_type);

/**
 * @brief Extract grpc-status from HTTP/2 trailers
 *
 * @param trailers Vector of HTTP/2 trailer headers
 * @return grpc-status value if found, std::nullopt otherwise
 *
 * gRPC status codes are sent in HTTP/2 trailers (HEADERS frame with END_STREAM).
 * If grpc-status is missing, the default is OK (0).
 *
 * Example trailer:
 *   grpc-status: 0
 *   grpc-message: OK
 */
std::optional<uint32_t> extract_grpc_status(
    const std::vector<std::pair<std::string, std::string>>& trailers
);

/**
 * @brief Extract grpc-message from HTTP/2 trailers
 *
 * @param trailers Vector of HTTP/2 trailer headers
 * @return grpc-message value if found, empty string otherwise
 *
 * grpc-message provides human-readable error description.
 * Must be percent-encoded as per gRPC spec.
 */
std::string extract_grpc_message(
    const std::vector<std::pair<std::string, std::string>>& trailers
);

/**
 * @brief Convert gRPC status code to HTTP status code
 *
 * @param grpc_status gRPC status code from trailers
 * @return Equivalent HTTP status code
 *
 * Mapping based on gRPC HTTP/2 spec:
 * - OK (0) → 200
 * - CANCELLED (1) → 499 (Client Closed Request)
 * - INVALID_ARGUMENT (3) → 400
 * - NOT_FOUND (5) → 404
 * - PERMISSION_DENIED (7) → 403
 * - RESOURCE_EXHAUSTED (8) → 429
 * - UNIMPLEMENTED (12) → 501
 * - UNAVAILABLE (14) → 503
 * - UNAUTHENTICATED (16) → 401
 * - All others → 500
 */
int grpc_status_to_http(uint32_t grpc_status);

/**
 * @brief Get human-readable name for gRPC status code
 *
 * @param grpc_status gRPC status code
 * @return Status name (e.g., "OK", "INVALID_ARGUMENT", "UNKNOWN")
 */
std::string_view grpc_status_name(uint32_t grpc_status);

}  // namespace titan::http
