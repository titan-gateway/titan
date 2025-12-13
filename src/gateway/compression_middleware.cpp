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

// Titan Compression Middleware - Implementation

#include "compression_middleware.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace titan::gateway {

// Thread-local compression contexts (lazy initialization)
thread_local std::unique_ptr<core::GzipContext> CompressionMiddleware::gzip_ctx_;
thread_local std::unique_ptr<core::ZstdContext> CompressionMiddleware::zstd_ctx_;
thread_local std::unique_ptr<core::BrotliContext> CompressionMiddleware::brotli_ctx_;

// Thread-local compression metrics
thread_local CompressionMetrics compression_metrics;

CompressionMiddleware::CompressionMiddleware(control::CompressionConfig config)
    : config_(std::move(config)) {}

MiddlewareResult CompressionMiddleware::process_response(ResponseContext& ctx) {
    const control::CompressionConfig& effective_config =
        ctx.route_match.compression_config.value_or(config_);

    if (!effective_config.enabled) {
        compression_metrics.skipped_disabled++;
        return MiddlewareResult::Continue;
    }

    if (ctx.response->body.size() < effective_config.min_size) {
        compression_metrics.skipped_too_small++;
        ctx.set_metadata("compression_skip_reason", "too_small");
        return MiddlewareResult::Continue;
    }

    if (is_breach_sensitive_path(ctx.request->path, effective_config)) {
        compression_metrics.skipped_breach_mitigation++;
        ctx.set_metadata("compression_skip_reason", "breach_sensitive_path");
        return MiddlewareResult::Continue;
    }

    if (effective_config.disable_when_setting_cookies && is_setting_cookies(*ctx.response)) {
        compression_metrics.skipped_breach_mitigation++;
        ctx.set_metadata("compression_skip_reason", "breach_setting_cookies");
        return MiddlewareResult::Continue;
    }

    auto content_type = ctx.response->get_header("Content-Type");
    if (!is_compressible(content_type, effective_config)) {
        compression_metrics.skipped_wrong_type++;
        ctx.set_metadata("compression_skip_reason", "uncompressible_type");
        return MiddlewareResult::Continue;
    }

    auto content_encoding = ctx.response->get_header("Content-Encoding");
    if (!content_encoding.empty() && content_encoding != "identity") {
        compression_metrics.skipped_wrong_type++;
        ctx.set_metadata("compression_skip_reason", "already_compressed");
        return MiddlewareResult::Continue;
    }

    auto encoding = negotiate_encoding(*ctx.request, effective_config);
    if (encoding == core::CompressionEncoding::NONE) {
        compression_metrics.skipped_client_unsupported++;
        ctx.set_metadata("compression_skip_reason", "client_unsupported");
        // Still add Vary header for proper caching
        ctx.response->add_middleware_header("Vary", "Accept-Encoding");
        return MiddlewareResult::Continue;
    }

    if (effective_config.precompressed.enabled) {
        if (try_serve_precompressed(ctx, encoding)) {
            compression_metrics.requests_compressed++;
            ctx.set_metadata("compression_type", "precompressed");
            ctx.set_metadata("compression_encoding", core::encoding_to_string(encoding));
            return MiddlewareResult::Continue;
        }
    }

    auto start_time = std::chrono::steady_clock::now();
    size_t original_size = ctx.response->body.size();
    bool use_streaming = (original_size > effective_config.streaming_threshold);

    bool success = false;
    if (use_streaming) {
        // Phase 3: Streaming compression (for large responses)
        success = compress_streaming(ctx, encoding);
    } else {
        // Phase 1: Buffered compression (for typical responses)
        std::span<const uint8_t> body_span = ctx.response->body;

        auto compressed = compress_buffered(body_span, encoding);
        if (!compressed.empty()) {
            ctx.response->body_storage = std::move(compressed);
            ctx.response->body = ctx.response->body_storage;  // Update span to point to storage
            success = true;
        }
    }

    if (!success) {
        // Compression failed - continue with uncompressed response
        ctx.set_metadata("compression_skip_reason", "compression_failed");
        return MiddlewareResult::Continue;
    }

    // Measure compression time
    auto end_time = std::chrono::steady_clock::now();
    auto compression_time =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    // Update headers
    update_headers(*ctx.response, encoding);

    // Calculate metrics
    size_t compressed_size = ctx.response->body.size();
    double ratio = static_cast<double>(original_size) / static_cast<double>(compressed_size);

    // Update metrics
    compression_metrics.requests_compressed++;
    compression_metrics.bytes_in += original_size;
    compression_metrics.bytes_out += compressed_size;
    compression_metrics.compression_time_us += compression_time.count();

    switch (encoding) {
        case core::CompressionEncoding::GZIP:
            compression_metrics.gzip_count++;
            break;
        case core::CompressionEncoding::ZSTD:
            compression_metrics.zstd_count++;
            break;
        case core::CompressionEncoding::BROTLI:
            compression_metrics.brotli_count++;
            break;
        default:
            break;
    }

    // Store compression info in context metadata
    ctx.set_metadata("compression_encoding", core::encoding_to_string(encoding));
    ctx.set_metadata("compression_ratio", std::to_string(ratio));
    ctx.set_metadata("compression_time_us", std::to_string(compression_time.count()));
    ctx.set_metadata("compression_original_size", std::to_string(original_size));
    ctx.set_metadata("compression_compressed_size", std::to_string(compressed_size));

    return MiddlewareResult::Continue;
}

core::CompressionEncoding CompressionMiddleware::negotiate_encoding(
    const http::Request& req, const control::CompressionConfig& config) const {
    auto accept_encoding = req.get_header("Accept-Encoding");
    if (accept_encoding.empty()) {
        return core::CompressionEncoding::NONE;
    }

    struct EncodingWithQuality {
        core::CompressionEncoding encoding;
        float quality = 1.0f;
    };

    std::vector<EncodingWithQuality> supported_encodings;

    // Split by comma
    size_t pos = 0;
    while (pos < accept_encoding.size()) {
        // Skip whitespace
        while (pos < accept_encoding.size() && std::isspace(accept_encoding[pos])) {
            pos++;
        }

        // Find next comma or end
        size_t comma_pos = accept_encoding.find(',', pos);
        if (comma_pos == std::string_view::npos) {
            comma_pos = accept_encoding.size();
        }

        std::string_view token = accept_encoding.substr(pos, comma_pos - pos);

        // Parse encoding and quality
        size_t semicolon_pos = token.find(';');
        std::string_view encoding_name = token.substr(0, semicolon_pos);

        // Trim whitespace
        while (!encoding_name.empty() && std::isspace(encoding_name.front())) {
            encoding_name.remove_prefix(1);
        }
        while (!encoding_name.empty() && std::isspace(encoding_name.back())) {
            encoding_name.remove_suffix(1);
        }

        auto encoding = core::encoding_from_string(encoding_name);

        // Parse quality value if present
        float quality = 1.0f;
        if (semicolon_pos != std::string_view::npos) {
            std::string_view quality_part = token.substr(semicolon_pos + 1);
            size_t q_pos = quality_part.find('q');
            if (q_pos != std::string_view::npos) {
                size_t eq_pos = quality_part.find('=', q_pos);
                if (eq_pos != std::string_view::npos) {
                    std::string_view quality_str = quality_part.substr(eq_pos + 1);
                    // Trim whitespace
                    while (!quality_str.empty() && std::isspace(quality_str.front())) {
                        quality_str.remove_prefix(1);
                    }
                    while (!quality_str.empty() && std::isspace(quality_str.back())) {
                        quality_str.remove_suffix(1);
                    }
                    // Use from_chars for safe parsing
                    std::from_chars(quality_str.data(), quality_str.data() + quality_str.size(),
                                    quality);
                }
            }
        }

        if (encoding != core::CompressionEncoding::NONE) {
            std::string_view encoding_str = core::encoding_to_string(encoding);
            auto it = std::find(config.algorithms.begin(), config.algorithms.end(), encoding_str);
            if (it != config.algorithms.end()) {
                supported_encodings.push_back({encoding, quality});
            }
        }

        pos = comma_pos + 1;
    }

    if (supported_encodings.empty()) {
        return core::CompressionEncoding::NONE;
    }

    // Sort by quality (descending)
    std::sort(supported_encodings.begin(), supported_encodings.end(),
              [](const EncodingWithQuality& a, const EncodingWithQuality& b) {
                  return a.quality > b.quality;
              });

    float best_quality = supported_encodings[0].quality;
    for (const auto& algo : config.algorithms) {
        auto encoding = core::encoding_from_string(algo);
        for (const auto& candidate : supported_encodings) {
            if (candidate.encoding == encoding && candidate.quality >= best_quality * 0.99f) {
                return candidate.encoding;
            }
        }
    }

    return supported_encodings[0].encoding;
}

bool CompressionMiddleware::is_compressible(std::string_view content_type,
                                            const control::CompressionConfig& config) const {
    if (content_type.empty()) {
        return false;
    }

    for (const auto& excluded : config.excluded_content_types) {
        if (content_type.find(excluded) != std::string_view::npos) {
            return false;
        }
    }

    for (const auto& included : config.content_types) {
        if (content_type.find(included) != std::string_view::npos) {
            return true;
        }
    }

    return false;
}

bool CompressionMiddleware::is_breach_sensitive_path(
    std::string_view path, const control::CompressionConfig& config) const {
    // Check if path matches any BREACH-sensitive patterns
    for (const auto& pattern : config.disable_for_paths) {
        // Simple glob matching: support /* wildcard at end
        if (pattern.ends_with("/*")) {
            // Prefix match: /auth/* matches /auth/login, /auth/token, etc.
            std::string_view prefix(pattern.data(), pattern.size() - 2);  // Remove /*
            if (path.starts_with(prefix)) {
                return true;
            }
        } else {
            // Exact match
            if (path == pattern) {
                return true;
            }
        }
    }
    return false;
}

bool CompressionMiddleware::is_setting_cookies(const http::Response& response) const {
    // Check if response has Set-Cookie header
    // Set-Cookie indicates session management - likely contains sensitive tokens
    return response.has_header("Set-Cookie");
}

std::vector<uint8_t> CompressionMiddleware::compress_buffered(std::span<const uint8_t> body,
                                                              core::CompressionEncoding encoding) {
    switch (encoding) {
        case core::CompressionEncoding::GZIP:
            return get_gzip_context().compress(body.data(), body.size());
        case core::CompressionEncoding::ZSTD:
            return get_zstd_context().compress(body.data(), body.size());
        case core::CompressionEncoding::BROTLI:
            return get_brotli_context().compress(body.data(), body.size());
        default:
            return {};
    }
}

bool CompressionMiddleware::compress_streaming(ResponseContext& ctx,
                                               core::CompressionEncoding encoding) {
    std::vector<uint8_t> output;
    output.reserve(ctx.response->body.size() / 4);

    auto callback = [&output](const uint8_t* chunk, size_t chunk_size) {
        output.insert(output.end(), chunk, chunk + chunk_size);
    };

    bool success = false;
    switch (encoding) {
        case core::CompressionEncoding::GZIP:
            success = get_gzip_context().compress_stream(ctx.response->body.data(),
                                                         ctx.response->body.size(), callback, true);
            break;
        case core::CompressionEncoding::ZSTD:
            success = get_zstd_context().compress_stream(ctx.response->body.data(),
                                                         ctx.response->body.size(), callback, true);
            break;
        case core::CompressionEncoding::BROTLI:
            success = get_brotli_context().compress_stream(
                ctx.response->body.data(), ctx.response->body.size(), callback, true);
            break;
        default:
            return false;
    }

    if (success && !output.empty()) {
        ctx.response->body_storage = std::move(output);
        ctx.response->body = ctx.response->body_storage;
        return true;
    }

    return false;
}

bool CompressionMiddleware::try_serve_precompressed(ResponseContext& ctx,
                                                    core::CompressionEncoding encoding) {
    // Pre-compressed file serving optimization:
    // Check if pre-compressed version exists (.gz/.zst/.br) and serve directly
    // This eliminates runtime compression overhead for static assets

    // Get static file path from metadata (set by static file handler)
    auto file_path_meta = ctx.get_metadata("static_file_path");
    if (file_path_meta.empty()) {
        return false;  // Not a static file request
    }

    // Determine file extension for this encoding
    const char* ext = nullptr;
    switch (encoding) {
        case core::CompressionEncoding::BROTLI:
            ext = ".br";
            break;
        case core::CompressionEncoding::ZSTD:
            ext = ".zst";
            break;
        case core::CompressionEncoding::GZIP:
            ext = ".gz";
            break;
        default:
            return false;
    }

    // Build precompressed file path
    std::string compressed_path = std::string(file_path_meta) + ext;

    // Check if precompressed file exists
    std::filesystem::path compressed_file(compressed_path);
    if (!std::filesystem::exists(compressed_file)) {
        return false;  // Precompressed file doesn't exist
    }

    // Validate freshness: precompressed file must be newer than original
    std::filesystem::path original_file(file_path_meta);
    if (std::filesystem::exists(original_file)) {
        auto compressed_time = std::filesystem::last_write_time(compressed_file);
        auto original_time = std::filesystem::last_write_time(original_file);
        if (compressed_time < original_time) {
            return false;  // Stale precompressed file
        }
    }

    // Read precompressed file
    std::ifstream file(compressed_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> compressed_data(size);
    if (!file.read(reinterpret_cast<char*>(compressed_data.data()), size)) {
        return false;
    }

    // Serve precompressed file
    size_t original_size = ctx.response->body.size();  // For metrics
    ctx.response->body_storage = std::move(compressed_data);
    ctx.response->body = ctx.response->body_storage;

    // Update headers using hybrid storage
    ctx.response->add_middleware_header("Content-Encoding", core::encoding_to_string(encoding));
    ctx.response->add_middleware_header("Vary", "Accept-Encoding");

    // Update metrics
    compression_metrics.bytes_in += original_size;
    compression_metrics.bytes_out += ctx.response->body.size();
    compression_metrics.precompressed_hits++;

    // Track encoding type
    switch (encoding) {
        case core::CompressionEncoding::GZIP:
            compression_metrics.gzip_count++;
            break;
        case core::CompressionEncoding::ZSTD:
            compression_metrics.zstd_count++;
            break;
        case core::CompressionEncoding::BROTLI:
            compression_metrics.brotli_count++;
            break;
        default:
            break;
    }

    return true;
}

void CompressionMiddleware::update_headers(http::Response& response,
                                           core::CompressionEncoding encoding) {
    // Add Content-Encoding header using hybrid storage
    response.add_middleware_header("Content-Encoding", core::encoding_to_string(encoding));

    // Add Vary header for proper caching
    auto existing_vary = response.get_header("Vary");
    if (existing_vary.empty()) {
        response.add_middleware_header("Vary", "Accept-Encoding");
    } else if (existing_vary.find("Accept-Encoding") == std::string_view::npos) {
        // Append to existing Vary header
        std::string new_vary = std::string(existing_vary) + ", Accept-Encoding";
        response.remove_header("Vary");
        response.add_middleware_header("Vary", new_vary);
    }

    // Convert strong ETag to weak ETag (compression changes byte representation)
    auto etag = response.get_header("ETag");
    if (!etag.empty() && etag[0] != 'W') {
        response.remove_header("ETag");
        std::string weak_etag = "W/" + std::string(etag);
        response.add_middleware_header("ETag", weak_etag);
    }

    // Update Content-Length if present
    // Note: Content-Length will be automatically updated by Response serialization
}

core::GzipContext& CompressionMiddleware::get_gzip_context() {
    if (!gzip_ctx_) {
        gzip_ctx_ = std::make_unique<core::GzipContext>(config_.levels.gzip);
    }
    return *gzip_ctx_;
}

core::ZstdContext& CompressionMiddleware::get_zstd_context() {
    if (!zstd_ctx_) {
        zstd_ctx_ = std::make_unique<core::ZstdContext>(config_.levels.zstd);
    }
    return *zstd_ctx_;
}

core::BrotliContext& CompressionMiddleware::get_brotli_context() {
    if (!brotli_ctx_) {
        brotli_ctx_ = std::make_unique<core::BrotliContext>(config_.levels.brotli);
    }
    return *brotli_ctx_;
}

}  // namespace titan::gateway
