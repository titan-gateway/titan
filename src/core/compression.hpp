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

// Titan Compression Contexts - Header
// Thread-local compression contexts for Gzip, Zstd, and Brotli

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

// Forward declarations for compression library types
struct z_stream_s;
typedef struct z_stream_s z_stream;
struct ZSTD_CCtx_s;
typedef struct ZSTD_CCtx_s ZSTD_CCtx;
struct BrotliEncoderStateStruct;
typedef struct BrotliEncoderStateStruct BrotliEncoderState;

namespace titan::core {

/// Compression encoding types
enum class CompressionEncoding : uint8_t { NONE = 0, GZIP = 1, ZSTD = 2, BROTLI = 3 };

/// Convert encoding enum to string (for Content-Encoding header)
[[nodiscard]] constexpr const char* encoding_to_string(CompressionEncoding encoding) noexcept {
    switch (encoding) {
        case CompressionEncoding::GZIP:
            return "gzip";
        case CompressionEncoding::ZSTD:
            return "zstd";
        case CompressionEncoding::BROTLI:
            return "br";
        default:
            return "";
    }
}

/// Case-insensitive string equality check for encoding names
[[nodiscard]] constexpr bool encoding_equals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (a[i] + 32) : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (b[i] + 32) : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

/// Parse encoding from string (Accept-Encoding header parsing)
/// Case-insensitive comparison per HTTP best practices
[[nodiscard]] constexpr CompressionEncoding encoding_from_string(
    std::string_view encoding) noexcept {
    if (encoding_equals(encoding, "gzip")) {
        return CompressionEncoding::GZIP;
    } else if (encoding_equals(encoding, "zstd")) {
        return CompressionEncoding::ZSTD;
    } else if (encoding_equals(encoding, "br") || encoding_equals(encoding, "brotli")) {
        return CompressionEncoding::BROTLI;
    } else {
        return CompressionEncoding::NONE;
    }
}

/// Gzip compression context (thread-local, reusable)
class GzipContext {
public:
    /// Constructor with compression level (1-9, default 6)
    explicit GzipContext(int level = 6);

    /// Destructor (cleanup zlib resources)
    ~GzipContext();

    // Non-copyable, non-movable
    GzipContext(const GzipContext&) = delete;
    GzipContext& operator=(const GzipContext&) = delete;
    GzipContext(GzipContext&&) = delete;
    GzipContext& operator=(GzipContext&&) = delete;

    /// Compress data (full buffered compression)
    /// Returns compressed data or empty vector on error
    [[nodiscard]] std::vector<uint8_t> compress(const uint8_t* data, size_t size);

    /// Compress data with streaming (callback invoked for each chunk)
    /// Returns true on success, false on error
    [[nodiscard]] bool compress_stream(const uint8_t* data, size_t size,
                                       std::function<void(const uint8_t*, size_t)> callback,
                                       bool finish = true);

    /// Reset context for reuse (keeps allocated memory)
    void reset();

    /// Get compression level
    [[nodiscard]] int level() const noexcept { return level_; }

private:
    z_stream* stream_;
    int level_;
    bool initialized_;
};

/// Zstandard compression context (thread-local, reusable)
class ZstdContext {
public:
    /// Constructor with compression level (1-22, default 5)
    explicit ZstdContext(int level = 5);

    /// Destructor (cleanup zstd resources)
    ~ZstdContext();

    // Non-copyable, non-movable
    ZstdContext(const ZstdContext&) = delete;
    ZstdContext& operator=(const ZstdContext&) = delete;
    ZstdContext(ZstdContext&&) = delete;
    ZstdContext& operator=(ZstdContext&&) = delete;

    /// Compress data (full buffered compression)
    /// Returns compressed data or empty vector on error
    [[nodiscard]] std::vector<uint8_t> compress(const uint8_t* data, size_t size);

    /// Compress data with streaming (callback invoked for each chunk)
    /// Returns true on success, false on error
    [[nodiscard]] bool compress_stream(const uint8_t* data, size_t size,
                                       std::function<void(const uint8_t*, size_t)> callback,
                                       bool finish = true);

    /// Reset context for reuse (keeps allocated memory)
    void reset();

    /// Get compression level
    [[nodiscard]] int level() const noexcept { return level_; }

private:
    ZSTD_CCtx* cstream_;
    int level_;
};

/// Brotli compression context (thread-local, reusable)
class BrotliContext {
public:
    /// Constructor with quality level (0-11, default 4)
    explicit BrotliContext(int quality = 4);

    /// Destructor (cleanup brotli resources)
    ~BrotliContext();

    // Non-copyable, non-movable
    BrotliContext(const BrotliContext&) = delete;
    BrotliContext& operator=(const BrotliContext&) = delete;
    BrotliContext(BrotliContext&&) = delete;
    BrotliContext& operator=(BrotliContext&&) = delete;

    /// Compress data (full buffered compression)
    /// Returns compressed data or empty vector on error
    [[nodiscard]] std::vector<uint8_t> compress(const uint8_t* data, size_t size);

    /// Compress data with streaming (callback invoked for each chunk)
    /// Returns true on success, false on error
    [[nodiscard]] bool compress_stream(const uint8_t* data, size_t size,
                                       std::function<void(const uint8_t*, size_t)> callback,
                                       bool finish = true);

    /// Reset context for reuse (keeps allocated memory)
    void reset();

    /// Get quality level
    [[nodiscard]] int quality() const noexcept { return quality_; }

private:
    BrotliEncoderState* state_;
    int quality_;
};

}  // namespace titan::core
