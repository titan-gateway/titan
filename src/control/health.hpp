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


// Titan Health Checks - Header
// Provides health check endpoints and status reporting

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace titan::control {

/// Health status levels
enum class HealthStatus {
    Healthy,    // All systems operational
    Degraded,   // Some issues but still serving traffic
    Unhealthy   // Critical issues, should not receive traffic
};

/// Backend health information
struct BackendHealth {
    std::string host;
    uint16_t port = 0;
    HealthStatus status = HealthStatus::Healthy;
    uint64_t successful_checks = 0;
    uint64_t failed_checks = 0;
    std::chrono::milliseconds last_check_latency{0};
    std::chrono::system_clock::time_point last_check_time;
    std::string last_error;
};

/// Upstream health information
struct UpstreamHealth {
    std::string name;
    HealthStatus status = HealthStatus::Healthy;
    size_t healthy_backends = 0;
    size_t total_backends = 0;
    std::vector<BackendHealth> backends;
};

/// Server health information
struct ServerHealth {
    HealthStatus status = HealthStatus::Healthy;
    std::chrono::seconds uptime{0};
    uint64_t total_requests = 0;
    uint64_t active_connections = 0;
    uint64_t total_errors = 0;
    std::chrono::system_clock::time_point start_time;
    std::vector<UpstreamHealth> upstreams;
};

/// Health checker for backends
class HealthChecker {
public:
    HealthChecker() = default;
    ~HealthChecker() = default;

    // Non-copyable, non-movable
    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    /// Start health checking
    void start();

    /// Stop health checking
    void stop();

    /// Check if health checker is running
    [[nodiscard]] bool is_running() const noexcept {
        return running_;
    }

    /// Manually check a specific backend
    [[nodiscard]] BackendHealth check_backend(
        std::string_view host,
        uint16_t port,
        std::string_view path = "/health",
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    /// Get current server health
    [[nodiscard]] ServerHealth get_server_health() const;

    /// Update backend health status
    void update_backend_health(
        std::string_view upstream_name,
        std::string_view host,
        uint16_t port,
        HealthStatus status);

    /// Record request
    void record_request();

    /// Record error
    void record_error();

    /// Update active connections
    void update_active_connections(uint64_t count);

private:
    bool running_ = false;
    std::chrono::system_clock::time_point start_time_ = std::chrono::system_clock::now();
    uint64_t total_requests_ = 0;
    uint64_t total_errors_ = 0;
    uint64_t active_connections_ = 0;

    // Backend health state (simplified for MVP)
    struct BackendState {
        std::string upstream_name;
        std::string host;
        uint16_t port;
        HealthStatus status;
        uint64_t successful_checks;
        uint64_t failed_checks;
        std::chrono::system_clock::time_point last_check_time;
        std::chrono::milliseconds last_check_latency;
        std::string last_error;
    };

    std::vector<BackendState> backend_states_;

    /// Find or create backend state
    BackendState* find_backend_state(
        std::string_view upstream_name,
        std::string_view host,
        uint16_t port);
};

/// Health check response builder
class HealthResponse {
public:
    /// Build JSON health response
    [[nodiscard]] static std::string to_json(const ServerHealth& health);

    /// Build simple text response (for basic health checks)
    [[nodiscard]] static std::string to_text(const ServerHealth& health);

    /// Determine HTTP status code based on health
    [[nodiscard]] static uint16_t to_http_status(HealthStatus status) noexcept {
        switch (status) {
            case HealthStatus::Healthy:
                return 200;  // OK
            case HealthStatus::Degraded:
                return 200;  // Still OK but with warnings
            case HealthStatus::Unhealthy:
                return 503;  // Service Unavailable
        }
        return 500;  // Internal Server Error
    }
};

} // namespace titan::control
