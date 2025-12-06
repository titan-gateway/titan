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

// Titan Admin Server - Implementation
// Lightweight HTTP server for internal admin endpoints

#include "admin_server.hpp"

#include <atomic>

#include <nlohmann/json.hpp>

#include "../control/prometheus.hpp"
#include "socket.hpp"

// Forward declare global for metrics
namespace titan::core {
extern std::atomic<const gateway::UpstreamManager*> g_upstream_manager_for_metrics;
}

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <string>

namespace titan::core {

AdminServer::AdminServer(const control::Config& config,
                         const gateway::UpstreamManager* upstream_manager,
                         RevocationQueue* revocation_queue)
    : config_(config), upstream_manager_(upstream_manager), revocation_queue_(revocation_queue) {}

AdminServer::~AdminServer() {
    stop();
}

std::error_code AdminServer::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return std::make_error_code(std::errc::operation_in_progress);
    }

    // Create socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return std::error_code(errno, std::generic_category());
    }

    // Set SO_REUSEADDR to avoid "Address already in use" errors
    int reuse = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return std::error_code(errno, std::generic_category());
    }

    // Bind to metrics port (default 9090)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.metrics.port);

    // Bind to 127.0.0.1 for security (metrics should be internal only)
    // In Kubernetes, this is still accessible via pod IP
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int err = errno;
        close(listen_fd_);
        listen_fd_ = -1;
        return std::error_code(err, std::generic_category());
    }

    // Listen
    if (listen(listen_fd_, 32) < 0) {
        int err = errno;
        close(listen_fd_);
        listen_fd_ = -1;
        return std::error_code(err, std::generic_category());
    }

    running_.store(true, std::memory_order_relaxed);
    return {};
}

void AdminServer::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void AdminServer::run() {
    while (running_.load(std::memory_order_relaxed)) {
        // Accept connection (blocking)
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, try again
            }
            if (!running_.load(std::memory_order_relaxed)) {
                break;  // Server stopped
            }
            continue;  // Other error, continue accepting
        }

        // Handle connection (blocking)
        handle_connection(client_fd);
        close(client_fd);
    }
}

void AdminServer::handle_connection(int client_fd) {
    // Read request (simple blocking read)
    char buffer[4096];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return;
    }
    buffer[n] = '\0';

    // Parse request (minimal parser)
    auto req = parse_request(buffer, n);
    if (!req.valid) {
        send_response(client_fd, 400, "text/plain", "Bad Request");
        return;
    }

    // Route request
    if (req.method == "GET") {
        if (req.path == "/_health" || req.path == "/health") {
            // Health endpoint
            std::string body = R"({"status":"healthy","version":"0.1.0"})";
            send_response(client_fd, 200, "application/json", body);
            return;
        }

        if (req.path == "/metrics" || req.path == config_.metrics.path) {
            // Metrics endpoint - read from global set by worker 0
            const gateway::UpstreamManager* upstream_mgr =
                titan::core::g_upstream_manager_for_metrics.load(std::memory_order_acquire);

            std::string body = control::PrometheusExporter::export_circuit_breaker_metrics(
                upstream_mgr, worker_id_, "titan");
            send_response(client_fd, 200, "text/plain; version=0.0.4", body);
            return;
        }
    }

    // POST endpoints
    if (req.method == "POST") {
        if (req.path == "/_admin/jwt/revoke") {
            // JWT revocation endpoint
            if (!revocation_queue_) {
                send_response(client_fd, 503, "application/json",
                              R"({"error":"service_unavailable","message":"Revocation not enabled"})");
                return;
            }

            // Extract body from request (find "\r\n\r\n" then parse JSON)
            const char* body_start = std::strstr(buffer, "\r\n\r\n");
            if (!body_start) {
                send_response(client_fd, 400, "application/json",
                              R"({"error":"bad_request","message":"Missing request body"})");
                return;
            }
            body_start += 4;  // Skip "\r\n\r\n"

            // Parse JSON body
            try {
                auto json = nlohmann::json::parse(body_start);

                // Extract jti (required)
                if (!json.contains("jti") || !json["jti"].is_string()) {
                    send_response(client_fd, 400, "application/json",
                                  R"({"error":"bad_request","message":"Missing or invalid 'jti' field"})");
                    return;
                }

                // Extract exp (required)
                if (!json.contains("exp") || !json["exp"].is_number_unsigned()) {
                    send_response(client_fd, 400, "application/json",
                                  R"json({"error":"bad_request","message":"Missing or invalid 'exp' field (must be Unix timestamp)"})json");
                    return;
                }

                std::string jti = json["jti"];
                uint64_t exp = json["exp"];

                // Push to global revocation queue
                revocation_queue_->push({std::move(jti), exp});

                // Success response
                std::string response_body =
                    R"({"status":"ok","message":"Token revoked successfully"})";
                send_response(client_fd, 200, "application/json", response_body);
                return;

            } catch (const nlohmann::json::exception& e) {
                std::string error_body = R"({"error":"bad_request","message":"Invalid JSON: )" +
                                         std::string(e.what()) + R"("})";
                send_response(client_fd, 400, "application/json", error_body);
                return;
            }
        }
    }

    // Not found
    send_response(client_fd, 404, "text/plain", "Not Found");
}

AdminServer::SimpleRequest AdminServer::parse_request(const char* data, size_t len) {
    SimpleRequest req;

    // Find first line (method and path)
    const char* line_end = static_cast<const char*>(memchr(data, '\n', len));
    if (!line_end) {
        return req;
    }

    // Parse "GET /path HTTP/1.1"
    const char* space1 = static_cast<const char*>(memchr(data, ' ', line_end - data));
    if (!space1) {
        return req;
    }

    const char* space2 = static_cast<const char*>(memchr(space1 + 1, ' ', line_end - space1 - 1));
    if (!space2) {
        return req;
    }

    // Extract method and path
    req.method = std::string(data, space1 - data);
    req.path = std::string(space1 + 1, space2 - space1 - 1);
    req.valid = true;

    return req;
}

void AdminServer::send_response(int fd, int status_code, std::string_view content_type,
                                std::string_view body) {
    std::ostringstream response;

    // Status line
    response << "HTTP/1.1 " << status_code << " ";
    switch (status_code) {
        case 200:
            response << "OK";
            break;
        case 400:
            response << "Bad Request";
            break;
        case 404:
            response << "Not Found";
            break;
        case 500:
            response << "Internal Server Error";
            break;
        default:
            response << "Unknown";
            break;
    }
    response << "\r\n";

    // Headers
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "Server: Titan-Admin/0.1.0\r\n";
    response << "\r\n";

    // Body
    response << body;

    // Send (blocking)
    std::string response_str = response.str();
    send(fd, response_str.data(), response_str.size(), 0);
}

}  // namespace titan::core
