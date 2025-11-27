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


// Titan Health Checks - Implementation

#include "health.hpp"

#include <algorithm>
#include <sstream>

namespace titan::control {

void HealthChecker::start() {
    running_ = true;
    start_time_ = std::chrono::system_clock::now();
}

void HealthChecker::stop() {
    running_ = false;
}

BackendHealth HealthChecker::check_backend(
    std::string_view host,
    uint16_t port,
    std::string_view path,
    std::chrono::milliseconds timeout) {

    BackendHealth health;
    health.host = host;
    health.port = port;
    health.last_check_time = std::chrono::system_clock::now();

    // TODO: Actual TCP connection and HTTP request
    // For MVP, we'll simulate a successful check
    // In Phase 6, this will use io_uring to make actual HTTP requests

    auto start = std::chrono::steady_clock::now();

    // Simulated check (always succeeds for now)
    health.status = HealthStatus::Healthy;

    auto end = std::chrono::steady_clock::now();
    health.last_check_latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    return health;
}

ServerHealth HealthChecker::get_server_health() const {
    ServerHealth health;

    auto now = std::chrono::system_clock::now();
    health.uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    health.start_time = start_time_;
    health.total_requests = total_requests_;
    health.active_connections = active_connections_;
    health.total_errors = total_errors_;

    // Determine overall status
    if (total_errors_ > 0 && total_requests_ > 0) {
        double error_rate = static_cast<double>(total_errors_) / static_cast<double>(total_requests_);
        if (error_rate >= 0.5) {
            health.status = HealthStatus::Unhealthy;
        } else if (error_rate >= 0.1) {
            health.status = HealthStatus::Degraded;
        } else {
            health.status = HealthStatus::Healthy;
        }
    } else {
        health.status = HealthStatus::Healthy;
    }

    // Group backends by upstream
    std::vector<std::string> upstream_names;
    for (const auto& backend : backend_states_) {
        if (std::find(upstream_names.begin(), upstream_names.end(), backend.upstream_name) == upstream_names.end()) {
            upstream_names.push_back(backend.upstream_name);
        }
    }

    // Build upstream health info
    for (const auto& upstream_name : upstream_names) {
        UpstreamHealth upstream_health;
        upstream_health.name = upstream_name;
        upstream_health.total_backends = 0;
        upstream_health.healthy_backends = 0;

        for (const auto& backend : backend_states_) {
            if (backend.upstream_name == upstream_name) {
                upstream_health.total_backends++;

                BackendHealth bh;
                bh.host = backend.host;
                bh.port = backend.port;
                bh.status = backend.status;
                bh.successful_checks = backend.successful_checks;
                bh.failed_checks = backend.failed_checks;
                bh.last_check_time = backend.last_check_time;
                bh.last_check_latency = backend.last_check_latency;
                bh.last_error = backend.last_error;

                upstream_health.backends.push_back(std::move(bh));

                if (backend.status == HealthStatus::Healthy) {
                    upstream_health.healthy_backends++;
                }
            }
        }

        // Determine upstream status
        if (upstream_health.healthy_backends == 0) {
            upstream_health.status = HealthStatus::Unhealthy;
        } else if (upstream_health.healthy_backends < upstream_health.total_backends) {
            upstream_health.status = HealthStatus::Degraded;
        } else {
            upstream_health.status = HealthStatus::Healthy;
        }

        health.upstreams.push_back(std::move(upstream_health));
    }

    return health;
}

void HealthChecker::update_backend_health(
    std::string_view upstream_name,
    std::string_view host,
    uint16_t port,
    HealthStatus status) {

    BackendState* state = find_backend_state(upstream_name, host, port);
    if (!state) {
        // Create new backend state
        BackendState new_state;
        new_state.upstream_name = upstream_name;
        new_state.host = host;
        new_state.port = port;
        new_state.status = status;
        new_state.successful_checks = 0;
        new_state.failed_checks = 0;
        new_state.last_check_time = std::chrono::system_clock::now();
        new_state.last_check_latency = std::chrono::milliseconds(0);

        backend_states_.push_back(std::move(new_state));
    } else {
        state->status = status;
        state->last_check_time = std::chrono::system_clock::now();

        if (status == HealthStatus::Healthy) {
            state->successful_checks++;
            state->last_error.clear();
        } else {
            state->failed_checks++;
        }
    }
}

void HealthChecker::record_request() {
    total_requests_++;
}

void HealthChecker::record_error() {
    total_errors_++;
}

void HealthChecker::update_active_connections(uint64_t count) {
    active_connections_ = count;
}

HealthChecker::BackendState* HealthChecker::find_backend_state(
    std::string_view upstream_name,
    std::string_view host,
    uint16_t port) {

    for (auto& state : backend_states_) {
        if (state.upstream_name == upstream_name &&
            state.host == host &&
            state.port == port) {
            return &state;
        }
    }
    return nullptr;
}

// HealthResponse implementation

std::string HealthResponse::to_json(const ServerHealth& health) {
    std::ostringstream json;
    json << "{\n";

    // Status
    json << "  \"status\": \"";
    switch (health.status) {
        case HealthStatus::Healthy:
            json << "healthy";
            break;
        case HealthStatus::Degraded:
            json << "degraded";
            break;
        case HealthStatus::Unhealthy:
            json << "unhealthy";
            break;
    }
    json << "\",\n";

    // Uptime
    json << "  \"uptime_seconds\": " << health.uptime.count() << ",\n";

    // Metrics
    json << "  \"total_requests\": " << health.total_requests << ",\n";
    json << "  \"active_connections\": " << health.active_connections << ",\n";
    json << "  \"total_errors\": " << health.total_errors << ",\n";

    // Error rate
    if (health.total_requests > 0) {
        double error_rate = static_cast<double>(health.total_errors) / static_cast<double>(health.total_requests);
        json << "  \"error_rate\": " << error_rate << ",\n";
    } else {
        json << "  \"error_rate\": 0.0,\n";
    }

    // Upstreams
    json << "  \"upstreams\": [\n";
    for (size_t i = 0; i < health.upstreams.size(); ++i) {
        const auto& upstream = health.upstreams[i];

        json << "    {\n";
        json << "      \"name\": \"" << upstream.name << "\",\n";
        json << "      \"status\": \"";
        switch (upstream.status) {
            case HealthStatus::Healthy:
                json << "healthy";
                break;
            case HealthStatus::Degraded:
                json << "degraded";
                break;
            case HealthStatus::Unhealthy:
                json << "unhealthy";
                break;
        }
        json << "\",\n";
        json << "      \"healthy_backends\": " << upstream.healthy_backends << ",\n";
        json << "      \"total_backends\": " << upstream.total_backends << ",\n";
        json << "      \"backends\": [\n";

        for (size_t j = 0; j < upstream.backends.size(); ++j) {
            const auto& backend = upstream.backends[j];

            json << "        {\n";
            json << "          \"host\": \"" << backend.host << "\",\n";
            json << "          \"port\": " << backend.port << ",\n";
            json << "          \"status\": \"";
            switch (backend.status) {
                case HealthStatus::Healthy:
                    json << "healthy";
                    break;
                case HealthStatus::Degraded:
                    json << "degraded";
                    break;
                case HealthStatus::Unhealthy:
                    json << "unhealthy";
                    break;
            }
            json << "\",\n";
            json << "          \"successful_checks\": " << backend.successful_checks << ",\n";
            json << "          \"failed_checks\": " << backend.failed_checks << ",\n";
            json << "          \"last_check_latency_ms\": " << backend.last_check_latency.count();

            if (!backend.last_error.empty()) {
                json << ",\n          \"last_error\": \"" << backend.last_error << "\"";
            }

            json << "\n        }";
            if (j < upstream.backends.size() - 1) {
                json << ",";
            }
            json << "\n";
        }

        json << "      ]\n";
        json << "    }";
        if (i < health.upstreams.size() - 1) {
            json << ",";
        }
        json << "\n";
    }
    json << "  ]\n";

    json << "}\n";
    return json.str();
}

std::string HealthResponse::to_text(const ServerHealth& health) {
    std::ostringstream text;

    switch (health.status) {
        case HealthStatus::Healthy:
            text << "OK";
            break;
        case HealthStatus::Degraded:
            text << "DEGRADED";
            break;
        case HealthStatus::Unhealthy:
            text << "UNHEALTHY";
            break;
    }

    text << " - Uptime: " << health.uptime.count() << "s";
    text << ", Requests: " << health.total_requests;
    text << ", Errors: " << health.total_errors;
    text << ", Active: " << health.active_connections;

    return text.str();
}

} // namespace titan::control
