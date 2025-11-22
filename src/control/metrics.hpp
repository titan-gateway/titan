// Titan Metrics - Header
// Thread-local, lock-free metrics collection

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace titan::control {

/// Metrics snapshot at a point in time
struct MetricsSnapshot {
    // Request metrics
    uint64_t total_requests = 0;
    uint64_t total_errors = 0;
    uint64_t total_timeouts = 0;

    // Connection metrics
    uint64_t active_connections = 0;
    uint64_t total_connections = 0;
    uint64_t rejected_connections = 0;

    // Latency metrics (microseconds)
    uint64_t total_latency_us = 0;
    uint64_t min_latency_us = 0;
    uint64_t max_latency_us = 0;

    // Bandwidth metrics (bytes)
    uint64_t bytes_received = 0;
    uint64_t bytes_sent = 0;

    // HTTP status code counters
    uint64_t status_2xx = 0;  // Success
    uint64_t status_3xx = 0;  // Redirect
    uint64_t status_4xx = 0;  // Client error
    uint64_t status_5xx = 0;  // Server error

    // Derived metrics
    [[nodiscard]] double error_rate() const noexcept {
        if (total_requests == 0) return 0.0;
        return static_cast<double>(total_errors) / static_cast<double>(total_requests);
    }

    [[nodiscard]] double avg_latency_us() const noexcept {
        if (total_requests == 0) return 0.0;
        return static_cast<double>(total_latency_us) / static_cast<double>(total_requests);
    }
};

/// Thread-local metrics collector (lock-free)
class ThreadMetrics {
public:
    ThreadMetrics() = default;
    ~ThreadMetrics() = default;

    // Non-copyable, non-movable (std::atomic is not movable)
    ThreadMetrics(const ThreadMetrics&) = delete;
    ThreadMetrics& operator=(const ThreadMetrics&) = delete;
    ThreadMetrics(ThreadMetrics&&) = delete;
    ThreadMetrics& operator=(ThreadMetrics&&) = delete;

    /// Record a request
    void record_request() noexcept {
        total_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Record an error
    void record_error() noexcept {
        total_errors_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Record a timeout
    void record_timeout() noexcept {
        total_timeouts_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Record a new connection
    void record_connection() noexcept {
        total_connections_.fetch_add(1, std::memory_order_relaxed);
        active_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Record a closed connection
    void record_connection_close() noexcept {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }

    /// Record a rejected connection
    void record_connection_rejected() noexcept {
        rejected_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Record request latency
    void record_latency(std::chrono::microseconds latency) noexcept {
        uint64_t latency_us = latency.count();

        total_latency_us_.fetch_add(latency_us, std::memory_order_relaxed);

        // Update min (if this is smaller)
        uint64_t current_min = min_latency_us_.load(std::memory_order_relaxed);
        while (latency_us < current_min || current_min == 0) {
            if (min_latency_us_.compare_exchange_weak(current_min, latency_us,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
                break;
            }
        }

        // Update max (if this is larger)
        uint64_t current_max = max_latency_us_.load(std::memory_order_relaxed);
        while (latency_us > current_max) {
            if (max_latency_us_.compare_exchange_weak(current_max, latency_us,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
                break;
            }
        }
    }

    /// Record bytes received
    void record_bytes_received(uint64_t bytes) noexcept {
        bytes_received_.fetch_add(bytes, std::memory_order_relaxed);
    }

    /// Record bytes sent
    void record_bytes_sent(uint64_t bytes) noexcept {
        bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
    }

    /// Record HTTP status code
    void record_status_code(uint16_t status_code) noexcept {
        if (status_code >= 200 && status_code < 300) {
            status_2xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (status_code >= 300 && status_code < 400) {
            status_3xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (status_code >= 400 && status_code < 500) {
            status_4xx_.fetch_add(1, std::memory_order_relaxed);
        } else if (status_code >= 500 && status_code < 600) {
            status_5xx_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// Get current metrics snapshot
    [[nodiscard]] MetricsSnapshot snapshot() const noexcept {
        MetricsSnapshot snap;

        snap.total_requests = total_requests_.load(std::memory_order_relaxed);
        snap.total_errors = total_errors_.load(std::memory_order_relaxed);
        snap.total_timeouts = total_timeouts_.load(std::memory_order_relaxed);

        snap.active_connections = active_connections_.load(std::memory_order_relaxed);
        snap.total_connections = total_connections_.load(std::memory_order_relaxed);
        snap.rejected_connections = rejected_connections_.load(std::memory_order_relaxed);

        snap.total_latency_us = total_latency_us_.load(std::memory_order_relaxed);
        snap.min_latency_us = min_latency_us_.load(std::memory_order_relaxed);
        snap.max_latency_us = max_latency_us_.load(std::memory_order_relaxed);

        snap.bytes_received = bytes_received_.load(std::memory_order_relaxed);
        snap.bytes_sent = bytes_sent_.load(std::memory_order_relaxed);

        snap.status_2xx = status_2xx_.load(std::memory_order_relaxed);
        snap.status_3xx = status_3xx_.load(std::memory_order_relaxed);
        snap.status_4xx = status_4xx_.load(std::memory_order_relaxed);
        snap.status_5xx = status_5xx_.load(std::memory_order_relaxed);

        return snap;
    }

    /// Reset all counters (for testing)
    void reset() noexcept {
        total_requests_.store(0, std::memory_order_relaxed);
        total_errors_.store(0, std::memory_order_relaxed);
        total_timeouts_.store(0, std::memory_order_relaxed);

        active_connections_.store(0, std::memory_order_relaxed);
        total_connections_.store(0, std::memory_order_relaxed);
        rejected_connections_.store(0, std::memory_order_relaxed);

        total_latency_us_.store(0, std::memory_order_relaxed);
        min_latency_us_.store(0, std::memory_order_relaxed);
        max_latency_us_.store(0, std::memory_order_relaxed);

        bytes_received_.store(0, std::memory_order_relaxed);
        bytes_sent_.store(0, std::memory_order_relaxed);

        status_2xx_.store(0, std::memory_order_relaxed);
        status_3xx_.store(0, std::memory_order_relaxed);
        status_4xx_.store(0, std::memory_order_relaxed);
        status_5xx_.store(0, std::memory_order_relaxed);
    }

private:
    // Request counters
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_errors_{0};
    std::atomic<uint64_t> total_timeouts_{0};

    // Connection counters
    std::atomic<uint64_t> active_connections_{0};
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> rejected_connections_{0};

    // Latency counters
    std::atomic<uint64_t> total_latency_us_{0};
    std::atomic<uint64_t> min_latency_us_{0};
    std::atomic<uint64_t> max_latency_us_{0};

    // Bandwidth counters
    std::atomic<uint64_t> bytes_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};

    // HTTP status counters
    std::atomic<uint64_t> status_2xx_{0};
    std::atomic<uint64_t> status_3xx_{0};
    std::atomic<uint64_t> status_4xx_{0};
    std::atomic<uint64_t> status_5xx_{0};
};

/// Global metrics aggregator (collects from all threads)
class MetricsAggregator {
public:
    MetricsAggregator() = default;
    ~MetricsAggregator() = default;

    // Non-copyable, non-movable
    MetricsAggregator(const MetricsAggregator&) = delete;
    MetricsAggregator& operator=(const MetricsAggregator&) = delete;

    /// Register a thread's metrics
    void register_thread_metrics(ThreadMetrics* metrics) {
        thread_metrics_.push_back(metrics);
    }

    /// Aggregate metrics from all threads
    [[nodiscard]] MetricsSnapshot aggregate() const {
        MetricsSnapshot total;

        for (const auto* thread_metrics : thread_metrics_) {
            if (!thread_metrics) continue;

            auto snap = thread_metrics->snapshot();

            total.total_requests += snap.total_requests;
            total.total_errors += snap.total_errors;
            total.total_timeouts += snap.total_timeouts;

            total.active_connections += snap.active_connections;
            total.total_connections += snap.total_connections;
            total.rejected_connections += snap.rejected_connections;

            total.total_latency_us += snap.total_latency_us;

            // Min/max across all threads
            if (total.min_latency_us == 0 || (snap.min_latency_us > 0 && snap.min_latency_us < total.min_latency_us)) {
                total.min_latency_us = snap.min_latency_us;
            }
            if (snap.max_latency_us > total.max_latency_us) {
                total.max_latency_us = snap.max_latency_us;
            }

            total.bytes_received += snap.bytes_received;
            total.bytes_sent += snap.bytes_sent;

            total.status_2xx += snap.status_2xx;
            total.status_3xx += snap.status_3xx;
            total.status_4xx += snap.status_4xx;
            total.status_5xx += snap.status_5xx;
        }

        return total;
    }

    /// Get number of registered threads
    [[nodiscard]] size_t thread_count() const noexcept {
        return thread_metrics_.size();
    }

private:
    std::vector<ThreadMetrics*> thread_metrics_;
};

/// RAII helper for request timing
class RequestTimer {
public:
    explicit RequestTimer(ThreadMetrics& metrics)
        : metrics_(metrics)
        , start_time_(std::chrono::steady_clock::now()) {
        metrics_.record_request();
    }

    ~RequestTimer() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        metrics_.record_latency(duration);
    }

    // Non-copyable, non-movable
    RequestTimer(const RequestTimer&) = delete;
    RequestTimer& operator=(const RequestTimer&) = delete;

    /// Mark request as error
    void mark_error() noexcept {
        metrics_.record_error();
    }

    /// Mark request as timeout
    void mark_timeout() noexcept {
        metrics_.record_timeout();
    }

    /// Record status code
    void record_status(uint16_t code) noexcept {
        metrics_.record_status_code(code);
    }

private:
    ThreadMetrics& metrics_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace titan::control
