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

// Titan Compression Middleware - Header
// Response phase middleware for HTTP compression (Gzip, Zstd, Brotli)

#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../control/config.hpp"
#include "../core/compression.hpp"
#include "pipeline.hpp"

namespace titan::gateway {

/// Compression middleware (Response Phase)
/// Compresses response bodies using Gzip, Zstd, or Brotli based on client Accept-Encoding
class CompressionMiddleware : public Middleware {
public:
    /// Constructor with configuration
    explicit CompressionMiddleware(control::CompressionConfig config);

    /// Destructor
    ~CompressionMiddleware() override = default;

    /// Process response (Response Phase only)
    [[nodiscard]] MiddlewareResult process_response(ResponseContext& ctx) override;

    /// Get middleware name
    [[nodiscard]] std::string_view name() const override { return "CompressionMiddleware"; }
    [[nodiscard]] std::string_view type() const override { return "compression"; }

private:
    control::CompressionConfig config_;

    // Thread-local compression contexts (initialized on first use)
    static thread_local std::unique_ptr<core::GzipContext> gzip_ctx_;
    static thread_local std::unique_ptr<core::ZstdContext> zstd_ctx_;
    static thread_local std::unique_ptr<core::BrotliContext> brotli_ctx_;

    [[nodiscard]] core::CompressionEncoding negotiate_encoding(
        const http::Request& req, const control::CompressionConfig& config) const;

    [[nodiscard]] bool is_compressible(std::string_view content_type,
                                       const control::CompressionConfig& config) const;

    [[nodiscard]] bool is_breach_sensitive_path(std::string_view path,
                                                const control::CompressionConfig& config) const;

    /// BREACH mitigation: Check if response is setting cookies
    [[nodiscard]] bool is_setting_cookies(const http::Response& response) const;

    /// Compress response body (buffered mode)
    [[nodiscard]] std::vector<uint8_t> compress_buffered(std::span<const uint8_t> body,
                                                         core::CompressionEncoding encoding);

    /// Compress response body (streaming mode) - Phase 3
    [[nodiscard]] bool compress_streaming(ResponseContext& ctx, core::CompressionEncoding encoding);

    /// Try to serve pre-compressed file - Phase 4
    [[nodiscard]] bool try_serve_precompressed(ResponseContext& ctx,
                                               core::CompressionEncoding encoding);

    /// Update response headers after compression
    void update_headers(http::Response& response, core::CompressionEncoding encoding);

    /// Get or create thread-local compression context
    [[nodiscard]] core::GzipContext& get_gzip_context();
    [[nodiscard]] core::ZstdContext& get_zstd_context();
    [[nodiscard]] core::BrotliContext& get_brotli_context();
};

/// Compression metrics (tracked per worker thread)
struct CompressionMetrics {
    uint64_t requests_compressed = 0;
    uint64_t requests_skipped = 0;
    uint64_t bytes_in = 0;
    uint64_t bytes_out = 0;
    uint64_t compression_time_us = 0;

    // Per-encoding counts
    uint64_t gzip_count = 0;
    uint64_t zstd_count = 0;
    uint64_t brotli_count = 0;

    // Skip reasons
    uint64_t skipped_too_small = 0;
    uint64_t skipped_wrong_type = 0;
    uint64_t skipped_client_unsupported = 0;
    uint64_t skipped_disabled = 0;
    uint64_t skipped_breach_mitigation = 0;  // BREACH attack mitigation

    // Precompressed file serving
    uint64_t precompressed_hits = 0;  // Successfully served precompressed files

    /// Calculate compression ratio (average)
    [[nodiscard]] double compression_ratio() const noexcept {
        if (bytes_in == 0)
            return 1.0;
        return static_cast<double>(bytes_in) / static_cast<double>(bytes_out);
    }

    /// Calculate average compression time (milliseconds)
    [[nodiscard]] double avg_compression_time_ms() const noexcept {
        if (requests_compressed == 0)
            return 0.0;
        return static_cast<double>(compression_time_us) / 1000.0 /
               static_cast<double>(requests_compressed);
    }
};

// Thread-local compression metrics
extern thread_local CompressionMetrics compression_metrics;

}  // namespace titan::gateway
