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

// Titan Prometheus Exporter - Header
// Formats metrics in Prometheus text exposition format

#pragma once

#include <sstream>
#include <string>
#include <string_view>

#include "../gateway/compression_middleware.hpp"
#include "../gateway/upstream.hpp"
#include "metrics.hpp"

namespace titan::control {

/// Prometheus metric types
enum class PrometheusType {
    Counter,    // Monotonically increasing counter
    Gauge,      // Value that can go up or down
    Histogram,  // Observations in buckets
    Summary     // Quantile summaries
};

/// Prometheus exporter
class PrometheusExporter {
public:
    PrometheusExporter() = default;
    ~PrometheusExporter() = default;

    // Non-copyable, non-movable
    PrometheusExporter(const PrometheusExporter&) = delete;
    PrometheusExporter& operator=(const PrometheusExporter&) = delete;

    /// Export metrics in Prometheus text format
    [[nodiscard]] static std::string export_metrics(const MetricsSnapshot& metrics,
                                                    std::string_view namespace_prefix = "titan") {
        std::ostringstream out;

        // Request metrics (counters)
        write_metric(out, namespace_prefix, "requests_total", "Total number of HTTP requests",
                     PrometheusType::Counter, metrics.total_requests);

        write_metric(out, namespace_prefix, "errors_total", "Total number of errors",
                     PrometheusType::Counter, metrics.total_errors);

        write_metric(out, namespace_prefix, "timeouts_total", "Total number of timeouts",
                     PrometheusType::Counter, metrics.total_timeouts);

        // Connection metrics
        write_metric(out, namespace_prefix, "connections_active",
                     "Current number of active connections", PrometheusType::Gauge,
                     metrics.active_connections);

        write_metric(out, namespace_prefix, "connections_total", "Total number of connections",
                     PrometheusType::Counter, metrics.total_connections);

        write_metric(out, namespace_prefix, "connections_rejected_total",
                     "Total number of rejected connections", PrometheusType::Counter,
                     metrics.rejected_connections);

        // Latency metrics (microseconds)
        write_metric(out, namespace_prefix, "latency_microseconds_total",
                     "Total latency in microseconds", PrometheusType::Counter,
                     metrics.total_latency_us);

        write_metric(out, namespace_prefix, "latency_microseconds_min",
                     "Minimum latency in microseconds", PrometheusType::Gauge,
                     metrics.min_latency_us);

        write_metric(out, namespace_prefix, "latency_microseconds_max",
                     "Maximum latency in microseconds", PrometheusType::Gauge,
                     metrics.max_latency_us);

        // Average latency (derived)
        write_metric(out, namespace_prefix, "latency_microseconds_avg",
                     "Average latency in microseconds", PrometheusType::Gauge,
                     metrics.avg_latency_us());

        // Bandwidth metrics
        write_metric(out, namespace_prefix, "bytes_received_total", "Total bytes received",
                     PrometheusType::Counter, metrics.bytes_received);

        write_metric(out, namespace_prefix, "bytes_sent_total", "Total bytes sent",
                     PrometheusType::Counter, metrics.bytes_sent);

        // HTTP status code metrics
        write_metric(out, namespace_prefix, "http_responses_total",
                     "Total HTTP responses by status class", PrometheusType::Counter,
                     metrics.status_2xx, {{"code", "2xx"}});

        write_metric(out, namespace_prefix, "http_responses_total",
                     "Total HTTP responses by status class", PrometheusType::Counter,
                     metrics.status_3xx, {{"code", "3xx"}});

        write_metric(out, namespace_prefix, "http_responses_total",
                     "Total HTTP responses by status class", PrometheusType::Counter,
                     metrics.status_4xx, {{"code", "4xx"}});

        write_metric(out, namespace_prefix, "http_responses_total",
                     "Total HTTP responses by status class", PrometheusType::Counter,
                     metrics.status_5xx, {{"code", "5xx"}});

        // Error rate (derived)
        write_metric(out, namespace_prefix, "error_rate", "Error rate (errors/requests)",
                     PrometheusType::Gauge, metrics.error_rate());

        return out.str();
    }

    /// Export circuit breaker metrics for all upstreams
    [[nodiscard]] static std::string export_circuit_breaker_metrics(
        const gateway::UpstreamManager* upstream_manager, uint32_t worker_id = 0,
        std::string_view namespace_prefix = "titan") {
        if (!upstream_manager) {
            return "";
        }

        std::ostringstream out;
        static std::string last_metric;

        // Iterate through all upstreams and their backends
        for (const auto& upstream_ptr : upstream_manager->upstreams()) {
            const auto& upstream = *upstream_ptr;
            for (const auto& backend : upstream.backends()) {
                if (!backend.circuit_breaker) {
                    continue;  // Skip backends without circuit breaker
                }

                std::vector<Label> labels = {{"backend", backend.address()},
                                             {"upstream", std::string(upstream.name())},
                                             {"worker", std::to_string(worker_id)}};

                // Circuit breaker state (0=CLOSED, 1=OPEN, 2=HALF_OPEN)
                auto state = backend.circuit_breaker->get_state();
                uint64_t state_value = 0;
                if (state == gateway::CircuitState::OPEN)
                    state_value = 1;
                else if (state == gateway::CircuitState::HALF_OPEN)
                    state_value = 2;

                write_metric(out, namespace_prefix, "circuit_breaker_state",
                             "Circuit breaker state (0=CLOSED, 1=OPEN, 2=HALF_OPEN)",
                             PrometheusType::Gauge, state_value, labels);

                // Total failures
                write_metric(out, namespace_prefix, "circuit_breaker_failures_total",
                             "Total failures recorded by circuit breaker", PrometheusType::Counter,
                             backend.circuit_breaker->get_total_failures(), labels);

                // Total successes
                write_metric(out, namespace_prefix, "circuit_breaker_successes_total",
                             "Total successes recorded by circuit breaker", PrometheusType::Counter,
                             backend.circuit_breaker->get_total_successes(), labels);

                // Rejected requests
                write_metric(out, namespace_prefix, "circuit_breaker_rejected_total",
                             "Total requests rejected by circuit breaker", PrometheusType::Counter,
                             backend.circuit_breaker->get_rejected_requests(), labels);

                // State transitions
                write_metric(out, namespace_prefix, "circuit_breaker_transitions_total",
                             "Total circuit breaker state transitions", PrometheusType::Counter,
                             backend.circuit_breaker->get_state_transitions(), labels);
            }
        }

        return out.str();
    }

    /// Export compression metrics
    [[nodiscard]] static std::string export_compression_metrics(
        const gateway::CompressionMetrics& metrics, uint32_t worker_id = 0,
        std::string_view namespace_prefix = "titan") {
        std::ostringstream out;

        std::vector<Label> worker_label = {{"worker", std::to_string(worker_id)}};

        write_metric(out, namespace_prefix, "compression_requests_total",
                     "Total requests compressed", PrometheusType::Counter,
                     metrics.requests_compressed, worker_label);

        write_metric(out, namespace_prefix, "compression_bytes_in_total",
                     "Total uncompressed bytes", PrometheusType::Counter, metrics.bytes_in,
                     worker_label);

        write_metric(out, namespace_prefix, "compression_bytes_out_total", "Total compressed bytes",
                     PrometheusType::Counter, metrics.bytes_out, worker_label);

        write_metric(out, namespace_prefix, "compression_time_microseconds_total",
                     "Total compression time in microseconds", PrometheusType::Counter,
                     metrics.compression_time_us, worker_label);

        write_metric(out, namespace_prefix, "compression_ratio", "Average compression ratio",
                     PrometheusType::Gauge, metrics.compression_ratio(), worker_label);

        write_metric(out, namespace_prefix, "compression_time_milliseconds_avg",
                     "Average compression time in milliseconds", PrometheusType::Gauge,
                     metrics.avg_compression_time_ms(), worker_label);

        write_metric(out, namespace_prefix, "compression_algorithm_total",
                     "Requests compressed by algorithm", PrometheusType::Counter,
                     metrics.gzip_count,
                     {{"algorithm", "gzip"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_algorithm_total",
                     "Requests compressed by algorithm", PrometheusType::Counter,
                     metrics.zstd_count,
                     {{"algorithm", "zstd"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_algorithm_total",
                     "Requests compressed by algorithm", PrometheusType::Counter,
                     metrics.brotli_count,
                     {{"algorithm", "brotli"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_skipped_total",
                     "Requests skipped by reason", PrometheusType::Counter,
                     metrics.skipped_too_small,
                     {{"reason", "too_small"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_skipped_total",
                     "Requests skipped by reason", PrometheusType::Counter,
                     metrics.skipped_wrong_type,
                     {{"reason", "wrong_type"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_skipped_total",
                     "Requests skipped by reason", PrometheusType::Counter,
                     metrics.skipped_client_unsupported,
                     {{"reason", "client_unsupported"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_skipped_total",
                     "Requests skipped by reason", PrometheusType::Counter,
                     metrics.skipped_disabled,
                     {{"reason", "disabled"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_skipped_total",
                     "Requests skipped by reason", PrometheusType::Counter,
                     metrics.skipped_breach_mitigation,
                     {{"reason", "breach_mitigation"}, {"worker", std::to_string(worker_id)}});

        write_metric(out, namespace_prefix, "compression_precompressed_total",
                     "Requests served from precompressed files", PrometheusType::Counter,
                     metrics.precompressed_hits, worker_label);

        return out.str();
    }

private:
    /// Label for Prometheus metrics
    struct Label {
        std::string name;
        std::string value;
    };

    /// Write HELP and TYPE lines
    static void write_header(std::ostringstream& out, std::string_view namespace_prefix,
                             std::string_view metric_name, std::string_view help,
                             PrometheusType type) {
        // HELP line
        out << "# HELP " << namespace_prefix << "_" << metric_name << " " << help << "\n";

        // TYPE line
        out << "# TYPE " << namespace_prefix << "_" << metric_name << " ";
        switch (type) {
            case PrometheusType::Counter:
                out << "counter";
                break;
            case PrometheusType::Gauge:
                out << "gauge";
                break;
            case PrometheusType::Histogram:
                out << "histogram";
                break;
            case PrometheusType::Summary:
                out << "summary";
                break;
        }
        out << "\n";
    }

    /// Write metric with uint64_t value
    static void write_metric(std::ostringstream& out, std::string_view namespace_prefix,
                             std::string_view metric_name, std::string_view help,
                             PrometheusType type, uint64_t value,
                             const std::vector<Label>& labels = {}) {
        static std::string last_metric;
        std::string full_name = std::string(namespace_prefix) + "_" + std::string(metric_name);

        // Only write header if this is a new metric
        if (full_name != last_metric) {
            write_header(out, namespace_prefix, metric_name, help, type);
            last_metric = full_name;
        }

        // Metric line
        out << full_name;

        // Labels
        if (!labels.empty()) {
            out << "{";
            for (size_t i = 0; i < labels.size(); ++i) {
                out << labels[i].name << "=\"" << labels[i].value << "\"";
                if (i < labels.size() - 1) {
                    out << ",";
                }
            }
            out << "}";
        }

        out << " " << value << "\n";
    }

    /// Write metric with double value
    static void write_metric(std::ostringstream& out, std::string_view namespace_prefix,
                             std::string_view metric_name, std::string_view help,
                             PrometheusType type, double value,
                             const std::vector<Label>& labels = {}) {
        static std::string last_metric;
        std::string full_name = std::string(namespace_prefix) + "_" + std::string(metric_name);

        // Only write header if this is a new metric
        if (full_name != last_metric) {
            write_header(out, namespace_prefix, metric_name, help, type);
            last_metric = full_name;
        }

        // Metric line
        out << full_name;

        // Labels
        if (!labels.empty()) {
            out << "{";
            for (size_t i = 0; i < labels.size(); ++i) {
                out << labels[i].name << "=\"" << labels[i].value << "\"";
                if (i < labels.size() - 1) {
                    out << ",";
                }
            }
            out << "}";
        }

        out << " " << value << "\n";
    }
};

}  // namespace titan::control
