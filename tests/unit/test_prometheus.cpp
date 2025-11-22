// Prometheus Exporter Tests

#include "control/prometheus.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace titan::control;

TEST_CASE("Prometheus export basic metrics", "[control][prometheus]") {
    MetricsSnapshot metrics;
    metrics.total_requests = 1000;
    metrics.total_errors = 50;
    metrics.total_timeouts = 5;
    metrics.active_connections = 42;
    metrics.total_connections = 100;
    metrics.rejected_connections = 10;
    metrics.total_latency_us = 50000;
    metrics.min_latency_us = 100;
    metrics.max_latency_us = 5000;
    metrics.bytes_received = 1024000;
    metrics.bytes_sent = 512000;
    metrics.status_2xx = 900;
    metrics.status_3xx = 20;
    metrics.status_4xx = 30;
    metrics.status_5xx = 50;

    std::string output = PrometheusExporter::export_metrics(metrics);

    SECTION("Contains request metrics") {
        REQUIRE(output.find("# HELP titan_requests_total") != std::string::npos);
        REQUIRE(output.find("# TYPE titan_requests_total counter") != std::string::npos);
        REQUIRE(output.find("titan_requests_total 1000") != std::string::npos);

        REQUIRE(output.find("# HELP titan_errors_total") != std::string::npos);
        REQUIRE(output.find("titan_errors_total 50") != std::string::npos);

        REQUIRE(output.find("# HELP titan_timeouts_total") != std::string::npos);
        REQUIRE(output.find("titan_timeouts_total 5") != std::string::npos);
    }

    SECTION("Contains connection metrics") {
        REQUIRE(output.find("# HELP titan_connections_active") != std::string::npos);
        REQUIRE(output.find("# TYPE titan_connections_active gauge") != std::string::npos);
        REQUIRE(output.find("titan_connections_active 42") != std::string::npos);

        REQUIRE(output.find("titan_connections_total 100") != std::string::npos);
        REQUIRE(output.find("titan_connections_rejected_total 10") != std::string::npos);
    }

    SECTION("Contains latency metrics") {
        REQUIRE(output.find("titan_latency_microseconds_total 50000") != std::string::npos);
        REQUIRE(output.find("titan_latency_microseconds_min 100") != std::string::npos);
        REQUIRE(output.find("titan_latency_microseconds_max 5000") != std::string::npos);
        REQUIRE(output.find("titan_latency_microseconds_avg 50") != std::string::npos);  // 50000/1000
    }

    SECTION("Contains bandwidth metrics") {
        REQUIRE(output.find("titan_bytes_received_total 1024000") != std::string::npos);
        REQUIRE(output.find("titan_bytes_sent_total 512000") != std::string::npos);
    }

    SECTION("Contains HTTP status metrics with labels") {
        REQUIRE(output.find("# HELP titan_http_responses_total") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"2xx\"} 900") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"3xx\"} 20") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"4xx\"} 30") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"5xx\"} 50") != std::string::npos);
    }

    SECTION("Contains derived metrics") {
        REQUIRE(output.find("# HELP titan_error_rate") != std::string::npos);
        REQUIRE(output.find("# TYPE titan_error_rate gauge") != std::string::npos);
        REQUIRE(output.find("titan_error_rate 0.05") != std::string::npos);  // 50/1000
    }
}

TEST_CASE("Prometheus export with custom namespace", "[control][prometheus]") {
    MetricsSnapshot metrics;
    metrics.total_requests = 100;

    std::string output = PrometheusExporter::export_metrics(metrics, "custom");

    SECTION("Uses custom namespace") {
        REQUIRE(output.find("# HELP custom_requests_total") != std::string::npos);
        REQUIRE(output.find("# TYPE custom_requests_total counter") != std::string::npos);
        REQUIRE(output.find("custom_requests_total 100") != std::string::npos);
    }
}

TEST_CASE("Prometheus export with zero values", "[control][prometheus]") {
    MetricsSnapshot metrics;  // All zeros

    std::string output = PrometheusExporter::export_metrics(metrics);

    SECTION("Handles zero values") {
        REQUIRE(output.find("titan_requests_total 0") != std::string::npos);
        REQUIRE(output.find("titan_errors_total 0") != std::string::npos);
        REQUIRE(output.find("titan_connections_active 0") != std::string::npos);
    }

    SECTION("Error rate is 0.0 when no requests") {
        REQUIRE(output.find("titan_error_rate 0") != std::string::npos);
    }
}

TEST_CASE("Prometheus format validation", "[control][prometheus]") {
    MetricsSnapshot metrics;
    metrics.total_requests = 42;
    metrics.status_2xx = 40;
    metrics.status_4xx = 2;

    std::string output = PrometheusExporter::export_metrics(metrics);

    SECTION("Format follows Prometheus spec") {
        // Each metric should have HELP and TYPE before the value
        size_t help_pos = output.find("# HELP titan_requests_total");
        size_t type_pos = output.find("# TYPE titan_requests_total");
        size_t value_pos = output.find("titan_requests_total 42");

        REQUIRE(help_pos != std::string::npos);
        REQUIRE(type_pos != std::string::npos);
        REQUIRE(value_pos != std::string::npos);

        // HELP should come before TYPE
        REQUIRE(help_pos < type_pos);
        // TYPE should come before value
        REQUIRE(type_pos < value_pos);
    }

    SECTION("Labels are properly formatted") {
        // Labels should be in {key="value"} format
        REQUIRE(output.find("titan_http_responses_total{code=\"2xx\"} 40") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"4xx\"} 2") != std::string::npos);
    }

    SECTION("All lines end with newline") {
        // Count newlines
        size_t newline_count = 0;
        for (char c : output) {
            if (c == '\n') {
                newline_count++;
            }
        }
        REQUIRE(newline_count > 0);

        // Output should end with newline
        REQUIRE(output.back() == '\n');
    }
}

TEST_CASE("Prometheus export with realistic metrics", "[control][prometheus]") {
    // Simulate realistic production metrics
    MetricsSnapshot metrics;
    metrics.total_requests = 1000000;      // 1M requests
    metrics.total_errors = 1000;           // 0.1% error rate
    metrics.total_timeouts = 50;
    metrics.active_connections = 500;
    metrics.total_connections = 10000;
    metrics.rejected_connections = 100;
    metrics.total_latency_us = 500000000;  // 500s total
    metrics.min_latency_us = 50;           // 50us min
    metrics.max_latency_us = 100000;       // 100ms max
    metrics.bytes_received = 10737418240;  // 10GB
    metrics.bytes_sent = 21474836480;      // 20GB
    metrics.status_2xx = 900000;
    metrics.status_3xx = 50000;
    metrics.status_4xx = 45000;
    metrics.status_5xx = 5000;

    std::string output = PrometheusExporter::export_metrics(metrics);

    SECTION("All metrics present") {
        REQUIRE_FALSE(output.empty());

        // Request metrics
        REQUIRE(output.find("titan_requests_total 1000000") != std::string::npos);
        REQUIRE(output.find("titan_errors_total 1000") != std::string::npos);

        // Connection metrics
        REQUIRE(output.find("titan_connections_active 500") != std::string::npos);

        // Latency metrics
        REQUIRE(output.find("titan_latency_microseconds_min 50") != std::string::npos);
        REQUIRE(output.find("titan_latency_microseconds_max 100000") != std::string::npos);
        REQUIRE(output.find("titan_latency_microseconds_avg 500") != std::string::npos);  // 500000000/1000000

        // Bandwidth (large numbers)
        REQUIRE(output.find("titan_bytes_received_total 10737418240") != std::string::npos);
        REQUIRE(output.find("titan_bytes_sent_total 21474836480") != std::string::npos);

        // Status codes
        REQUIRE(output.find("titan_http_responses_total{code=\"2xx\"} 900000") != std::string::npos);

        // Error rate
        REQUIRE(output.find("titan_error_rate 0.001") != std::string::npos);  // 1000/1000000
    }
}

TEST_CASE("Integration with MetricsAggregator", "[control][prometheus]") {
    // Test that Prometheus can export aggregated metrics
    ThreadMetrics thread1;
    ThreadMetrics thread2;

    thread1.record_request();
    thread1.record_request();
    thread1.record_status_code(200);
    thread1.record_status_code(200);

    thread2.record_request();
    thread2.record_error();
    thread2.record_status_code(500);

    MetricsAggregator aggregator;
    aggregator.register_thread_metrics(&thread1);
    aggregator.register_thread_metrics(&thread2);

    auto total = aggregator.aggregate();
    std::string output = PrometheusExporter::export_metrics(total);

    SECTION("Aggregated metrics are exported correctly") {
        REQUIRE(output.find("titan_requests_total 3") != std::string::npos);  // 2 + 1
        REQUIRE(output.find("titan_errors_total 1") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"2xx\"} 2") != std::string::npos);
        REQUIRE(output.find("titan_http_responses_total{code=\"5xx\"} 1") != std::string::npos);
    }
}
