// Metrics Tests

#include <catch2/catch_test_macros.hpp>

#include "control/metrics.hpp"

using namespace titan::control;

TEST_CASE("ThreadMetrics basic operations", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("Initial state") {
        auto snap = metrics.snapshot();

        REQUIRE(snap.total_requests == 0);
        REQUIRE(snap.total_errors == 0);
        REQUIRE(snap.active_connections == 0);
        REQUIRE(snap.bytes_received == 0);
        REQUIRE(snap.bytes_sent == 0);
    }

    SECTION("Record requests") {
        metrics.record_request();
        metrics.record_request();
        metrics.record_request();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 3);
        REQUIRE(snap.total_errors == 0);
    }

    SECTION("Record errors") {
        metrics.record_request();
        metrics.record_error();
        metrics.record_request();
        metrics.record_error();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 2);
        REQUIRE(snap.total_errors == 2);
        REQUIRE(snap.error_rate() == 1.0);  // 2/2 = 100%
    }

    SECTION("Record timeouts") {
        metrics.record_timeout();
        metrics.record_timeout();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_timeouts == 2);
    }
}

TEST_CASE("ThreadMetrics connection tracking", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("Record connections") {
        metrics.record_connection();
        metrics.record_connection();
        metrics.record_connection();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_connections == 3);
        REQUIRE(snap.active_connections == 3);
        REQUIRE(snap.rejected_connections == 0);
    }

    SECTION("Close connections") {
        metrics.record_connection();
        metrics.record_connection();
        metrics.record_connection();

        metrics.record_connection_close();
        metrics.record_connection_close();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_connections == 3);
        REQUIRE(snap.active_connections == 1);  // 3 opened - 2 closed
    }

    SECTION("Rejected connections") {
        metrics.record_connection();
        metrics.record_connection_rejected();
        metrics.record_connection_rejected();

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_connections == 1);
        REQUIRE(snap.active_connections == 1);
        REQUIRE(snap.rejected_connections == 2);
    }
}

TEST_CASE("ThreadMetrics latency tracking", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("Record single latency") {
        metrics.record_request();
        metrics.record_latency(std::chrono::microseconds(1000));

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 1);
        REQUIRE(snap.total_latency_us == 1000);
        REQUIRE(snap.min_latency_us == 1000);
        REQUIRE(snap.max_latency_us == 1000);
        REQUIRE(snap.avg_latency_us() == 1000.0);
    }

    SECTION("Record multiple latencies") {
        metrics.record_request();
        metrics.record_latency(std::chrono::microseconds(100));

        metrics.record_request();
        metrics.record_latency(std::chrono::microseconds(500));

        metrics.record_request();
        metrics.record_latency(std::chrono::microseconds(300));

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 3);
        REQUIRE(snap.total_latency_us == 900);  // 100 + 500 + 300
        REQUIRE(snap.min_latency_us == 100);
        REQUIRE(snap.max_latency_us == 500);
        REQUIRE(snap.avg_latency_us() == 300.0);  // 900 / 3
    }

    SECTION("Min/max update correctly") {
        metrics.record_latency(std::chrono::microseconds(1000));
        metrics.record_latency(std::chrono::microseconds(500));   // New min
        metrics.record_latency(std::chrono::microseconds(2000));  // New max
        metrics.record_latency(std::chrono::microseconds(800));   // Between min and max

        auto snap = metrics.snapshot();
        REQUIRE(snap.min_latency_us == 500);
        REQUIRE(snap.max_latency_us == 2000);
    }
}

TEST_CASE("ThreadMetrics bandwidth tracking", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("Record bytes") {
        metrics.record_bytes_received(1024);
        metrics.record_bytes_received(2048);
        metrics.record_bytes_sent(512);
        metrics.record_bytes_sent(256);

        auto snap = metrics.snapshot();
        REQUIRE(snap.bytes_received == 3072);  // 1024 + 2048
        REQUIRE(snap.bytes_sent == 768);       // 512 + 256
    }
}

TEST_CASE("ThreadMetrics HTTP status tracking", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("2xx status codes") {
        metrics.record_status_code(200);
        metrics.record_status_code(201);
        metrics.record_status_code(204);

        auto snap = metrics.snapshot();
        REQUIRE(snap.status_2xx == 3);
        REQUIRE(snap.status_3xx == 0);
        REQUIRE(snap.status_4xx == 0);
        REQUIRE(snap.status_5xx == 0);
    }

    SECTION("3xx status codes") {
        metrics.record_status_code(301);
        metrics.record_status_code(302);

        auto snap = metrics.snapshot();
        REQUIRE(snap.status_3xx == 2);
    }

    SECTION("4xx status codes") {
        metrics.record_status_code(400);
        metrics.record_status_code(404);
        metrics.record_status_code(429);

        auto snap = metrics.snapshot();
        REQUIRE(snap.status_4xx == 3);
    }

    SECTION("5xx status codes") {
        metrics.record_status_code(500);
        metrics.record_status_code(502);
        metrics.record_status_code(503);

        auto snap = metrics.snapshot();
        REQUIRE(snap.status_5xx == 3);
    }

    SECTION("Mixed status codes") {
        metrics.record_status_code(200);
        metrics.record_status_code(404);
        metrics.record_status_code(500);
        metrics.record_status_code(200);

        auto snap = metrics.snapshot();
        REQUIRE(snap.status_2xx == 2);
        REQUIRE(snap.status_4xx == 1);
        REQUIRE(snap.status_5xx == 1);
    }
}

TEST_CASE("ThreadMetrics reset", "[control][metrics]") {
    ThreadMetrics metrics;

    metrics.record_request();
    metrics.record_error();
    metrics.record_connection();
    metrics.record_latency(std::chrono::microseconds(1000));
    metrics.record_bytes_received(1024);
    metrics.record_status_code(200);

    auto snap_before = metrics.snapshot();
    REQUIRE(snap_before.total_requests == 1);

    metrics.reset();

    auto snap_after = metrics.snapshot();
    REQUIRE(snap_after.total_requests == 0);
    REQUIRE(snap_after.total_errors == 0);
    REQUIRE(snap_after.active_connections == 0);
    REQUIRE(snap_after.total_latency_us == 0);
    REQUIRE(snap_after.bytes_received == 0);
    REQUIRE(snap_after.status_2xx == 0);
}

TEST_CASE("MetricsAggregator", "[control][metrics]") {
    MetricsAggregator aggregator;

    ThreadMetrics metrics1;
    ThreadMetrics metrics2;
    ThreadMetrics metrics3;

    aggregator.register_thread_metrics(&metrics1);
    aggregator.register_thread_metrics(&metrics2);
    aggregator.register_thread_metrics(&metrics3);

    SECTION("Thread count") {
        REQUIRE(aggregator.thread_count() == 3);
    }

    SECTION("Aggregate requests") {
        metrics1.record_request();
        metrics1.record_request();

        metrics2.record_request();
        metrics2.record_request();
        metrics2.record_request();

        metrics3.record_request();

        auto total = aggregator.aggregate();
        REQUIRE(total.total_requests == 6);  // 2 + 3 + 1
    }

    SECTION("Aggregate errors") {
        metrics1.record_error();
        metrics2.record_error();
        metrics2.record_error();

        auto total = aggregator.aggregate();
        REQUIRE(total.total_errors == 3);
    }

    SECTION("Aggregate connections") {
        metrics1.record_connection();
        metrics1.record_connection();

        metrics2.record_connection();
        metrics2.record_connection_close();

        metrics3.record_connection();

        auto total = aggregator.aggregate();
        REQUIRE(total.total_connections == 4);   // 2 + 1 + 1
        REQUIRE(total.active_connections == 3);  // 2 + 0 + 1
    }

    SECTION("Aggregate latency") {
        metrics1.record_latency(std::chrono::microseconds(100));
        metrics2.record_latency(std::chrono::microseconds(500));
        metrics3.record_latency(std::chrono::microseconds(300));

        auto total = aggregator.aggregate();
        REQUIRE(total.total_latency_us == 900);  // 100 + 500 + 300
        REQUIRE(total.min_latency_us == 100);
        REQUIRE(total.max_latency_us == 500);
    }

    SECTION("Aggregate bandwidth") {
        metrics1.record_bytes_received(1000);
        metrics2.record_bytes_received(2000);
        metrics3.record_bytes_sent(500);

        auto total = aggregator.aggregate();
        REQUIRE(total.bytes_received == 3000);
        REQUIRE(total.bytes_sent == 500);
    }

    SECTION("Aggregate status codes") {
        metrics1.record_status_code(200);
        metrics1.record_status_code(200);

        metrics2.record_status_code(404);

        metrics3.record_status_code(500);
        metrics3.record_status_code(500);

        auto total = aggregator.aggregate();
        REQUIRE(total.status_2xx == 2);
        REQUIRE(total.status_4xx == 1);
        REQUIRE(total.status_5xx == 2);
    }
}

TEST_CASE("RequestTimer RAII", "[control][metrics]") {
    ThreadMetrics metrics;

    SECTION("Basic timing") {
        {
            RequestTimer timer(metrics);
            // Simulate some work
        }

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 1);
        // Latency might be 0 if the timer completes extremely fast
        REQUIRE(snap.total_latency_us >= 0);
    }

    SECTION("Mark error") {
        {
            RequestTimer timer(metrics);
            timer.mark_error();
        }

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 1);
        REQUIRE(snap.total_errors == 1);
    }

    SECTION("Mark timeout") {
        {
            RequestTimer timer(metrics);
            timer.mark_timeout();
        }

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 1);
        REQUIRE(snap.total_timeouts == 1);
    }

    SECTION("Record status") {
        {
            RequestTimer timer(metrics);
            timer.record_status(200);
        }

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 1);
        REQUIRE(snap.status_2xx == 1);
    }

    SECTION("Multiple requests") {
        for (int i = 0; i < 5; ++i) {
            RequestTimer timer(metrics);
            // Simulate work
        }

        auto snap = metrics.snapshot();
        REQUIRE(snap.total_requests == 5);
    }
}

TEST_CASE("MetricsSnapshot derived metrics", "[control][metrics]") {
    SECTION("Error rate calculation") {
        MetricsSnapshot snap;
        snap.total_requests = 100;
        snap.total_errors = 10;

        REQUIRE(snap.error_rate() == 0.1);  // 10/100 = 10%
    }

    SECTION("Average latency calculation") {
        MetricsSnapshot snap;
        snap.total_requests = 5;
        snap.total_latency_us = 1000;

        REQUIRE(snap.avg_latency_us() == 200.0);  // 1000/5
    }

    SECTION("Zero requests") {
        MetricsSnapshot snap;
        snap.total_requests = 0;

        REQUIRE(snap.error_rate() == 0.0);
        REQUIRE(snap.avg_latency_us() == 0.0);
    }
}
