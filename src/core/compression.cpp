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

// Titan Compression Contexts - Implementation

#include "compression.hpp"

#include <brotli/encode.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>

namespace titan::core {

// ============================================================================
// GzipContext Implementation
// ============================================================================

GzipContext::GzipContext(int level) : stream_(new z_stream{}), level_(level), initialized_(false) {
    // Clamp level to valid range (1-9)
    level_ = std::clamp(level_, 1, 9);

    // Initialize zlib stream
    stream_->zalloc = Z_NULL;
    stream_->zfree = Z_NULL;
    stream_->opaque = Z_NULL;

    // deflateInit2: level, method=Z_DEFLATED, windowBits=15+16 (gzip format),
    // memLevel=8 (default), strategy=Z_DEFAULT_STRATEGY
    int ret = deflateInit2(stream_, level_, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    initialized_ = (ret == Z_OK);
}

GzipContext::~GzipContext() {
    if (initialized_ && stream_) {
        deflateEnd(stream_);
    }
    delete stream_;
}

std::vector<uint8_t> GzipContext::compress(const uint8_t* data, size_t size) {
    if (!initialized_ || !data || size == 0) {
        return {};
    }

    // Reset stream for reuse
    deflateReset(stream_);

    // Allocate output buffer (worst case: same size as input + 16 bytes overhead)
    std::vector<uint8_t> output;
    output.resize(size + 256);  // Extra space for gzip headers/trailers

    stream_->next_in = const_cast<uint8_t*>(data);
    stream_->avail_in = static_cast<unsigned int>(size);
    stream_->next_out = output.data();
    stream_->avail_out = static_cast<unsigned int>(output.size());

    // Compress with Z_FINISH (flush all data)
    int ret = deflate(stream_, Z_FINISH);
    if (ret != Z_STREAM_END) {
        return {};  // Compression failed
    }

    // Resize to actual compressed size
    size_t compressed_size = output.size() - stream_->avail_out;
    output.resize(compressed_size);

    return output;
}

bool GzipContext::compress_stream(const uint8_t* data, size_t size,
                                  std::function<void(const uint8_t*, size_t)> callback,
                                  bool finish) {
    if (!initialized_ || !data || size == 0 || !callback) {
        return false;
    }

    deflateReset(stream_);

    constexpr size_t CHUNK_SIZE = 16384;  // 16KB chunks
    std::vector<uint8_t> output(CHUNK_SIZE);

    stream_->next_in = const_cast<uint8_t*>(data);
    stream_->avail_in = static_cast<unsigned int>(size);

    int flush = finish ? Z_FINISH : Z_NO_FLUSH;

    do {
        stream_->next_out = output.data();
        stream_->avail_out = static_cast<unsigned int>(output.size());

        int ret = deflate(stream_, flush);
        if (ret == Z_STREAM_ERROR) {
            return false;
        }

        size_t have = output.size() - stream_->avail_out;
        if (have > 0) {
            callback(output.data(), have);
        }

        if (ret == Z_STREAM_END) {
            break;
        }
    } while (stream_->avail_out == 0);

    return true;
}

void GzipContext::reset() {
    if (initialized_) {
        deflateReset(stream_);
    }
}

// ============================================================================
// ZstdContext Implementation
// ============================================================================

ZstdContext::ZstdContext(int level) : cstream_(ZSTD_createCCtx()), level_(level) {
    // Clamp level to valid range (1-22)
    level_ = std::clamp(level_, 1, 22);

    if (cstream_) {
        ZSTD_CCtx_setParameter(cstream_, ZSTD_c_compressionLevel, level_);
    }
}

ZstdContext::~ZstdContext() {
    if (cstream_) {
        ZSTD_freeCCtx(cstream_);
    }
}

std::vector<uint8_t> ZstdContext::compress(const uint8_t* data, size_t size) {
    if (!cstream_ || !data || size == 0) {
        return {};
    }

    // Reset session for reuse (modern API)
    ZSTD_CCtx_reset(cstream_, ZSTD_reset_session_only);

    // Allocate output buffer (worst case: use ZSTD_compressBound)
    size_t max_output_size = ZSTD_compressBound(size);
    std::vector<uint8_t> output(max_output_size);

    // Prepare input/output buffers
    ZSTD_inBuffer input = {data, size, 0};
    ZSTD_outBuffer out = {output.data(), output.size(), 0};

    // Compress with ZSTD_e_end (finish stream)
    size_t ret = ZSTD_compressStream2(cstream_, &out, &input, ZSTD_e_end);
    if (ZSTD_isError(ret)) {
        return {};  // Compression failed
    }

    // Resize to actual compressed size
    output.resize(out.pos);
    return output;
}

bool ZstdContext::compress_stream(const uint8_t* data, size_t size,
                                  std::function<void(const uint8_t*, size_t)> callback,
                                  bool finish) {
    if (!cstream_ || !data || size == 0 || !callback) {
        return false;
    }

    ZSTD_CCtx_reset(cstream_, ZSTD_reset_session_only);

    constexpr size_t CHUNK_SIZE = 16384;  // 16KB chunks
    std::vector<uint8_t> output(CHUNK_SIZE);

    ZSTD_inBuffer input = {data, size, 0};

    ZSTD_EndDirective mode = finish ? ZSTD_e_end : ZSTD_e_continue;

    while (input.pos < input.size) {
        ZSTD_outBuffer out = {output.data(), output.size(), 0};

        size_t ret = ZSTD_compressStream2(cstream_, &out, &input, mode);
        if (ZSTD_isError(ret)) {
            return false;
        }

        if (out.pos > 0) {
            callback(output.data(), out.pos);
        }

        if (ret == 0) {
            break;  // Compression complete
        }
    }

    return true;
}

void ZstdContext::reset() {
    if (cstream_) {
        ZSTD_CCtx_reset(cstream_, ZSTD_reset_session_only);
    }
}

// ============================================================================
// BrotliContext Implementation
// ============================================================================

BrotliContext::BrotliContext(int quality)
    : state_(BrotliEncoderCreateInstance(nullptr, nullptr, nullptr)), quality_(quality) {
    // Clamp quality to valid range (0-11)
    quality_ = std::clamp(quality_, 0, 11);

    if (state_) {
        BrotliEncoderSetParameter(state_, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(quality_));
        // Use default window size (22 = 4MB)
        BrotliEncoderSetParameter(state_, BROTLI_PARAM_LGWIN, 22);
    }
}

BrotliContext::~BrotliContext() {
    if (state_) {
        BrotliEncoderDestroyInstance(state_);
    }
}

std::vector<uint8_t> BrotliContext::compress(const uint8_t* data, size_t size) {
    if (!state_ || !data || size == 0) {
        return {};
    }

    // Reset encoder (creates new instance - Brotli doesn't have a reset function)
    BrotliEncoderDestroyInstance(state_);
    state_ = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    BrotliEncoderSetParameter(state_, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(quality_));
    BrotliEncoderSetParameter(state_, BROTLI_PARAM_LGWIN, 22);

    // Allocate output buffer (worst case: same size as input + overhead)
    size_t max_output_size = BrotliEncoderMaxCompressedSize(size);
    std::vector<uint8_t> output(max_output_size);

    const uint8_t* input_ptr = data;
    size_t input_remaining = size;
    uint8_t* output_ptr = output.data();
    size_t output_remaining = output.size();

    // Compress with BROTLI_OPERATION_FINISH
    BROTLI_BOOL result =
        BrotliEncoderCompressStream(state_, BROTLI_OPERATION_FINISH, &input_remaining, &input_ptr,
                                    &output_remaining, &output_ptr, nullptr);

    if (!result) {
        return {};  // Compression failed
    }

    // Resize to actual compressed size
    size_t compressed_size = output.size() - output_remaining;
    output.resize(compressed_size);

    return output;
}

bool BrotliContext::compress_stream(const uint8_t* data, size_t size,
                                    std::function<void(const uint8_t*, size_t)> callback,
                                    bool finish) {
    if (!state_ || !data || size == 0 || !callback) {
        return false;
    }

    // Reset encoder state (Brotli doesn't have reset API, must recreate instance)
    BrotliEncoderDestroyInstance(state_);
    state_ = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    BrotliEncoderSetParameter(state_, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(quality_));
    BrotliEncoderSetParameter(state_, BROTLI_PARAM_LGWIN, 22);

    constexpr size_t CHUNK_SIZE = 16384;  // 16KB chunks
    std::vector<uint8_t> output(CHUNK_SIZE);

    const uint8_t* input_ptr = data;
    size_t input_remaining = size;

    BrotliEncoderOperation op = finish ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;

    do {
        uint8_t* output_ptr = output.data();
        size_t output_remaining = output.size();

        BROTLI_BOOL result = BrotliEncoderCompressStream(state_, op, &input_remaining, &input_ptr,
                                                         &output_remaining, &output_ptr, nullptr);

        if (!result) {
            return false;
        }

        size_t have = output.size() - output_remaining;
        if (have > 0) {
            callback(output.data(), have);
        }

        if (BrotliEncoderIsFinished(state_)) {
            break;
        }
    } while (input_remaining > 0 || BrotliEncoderHasMoreOutput(state_));

    return true;
}

void BrotliContext::reset() {
    if (state_) {
        // Brotli doesn't have a reset function - recreate instance
        BrotliEncoderDestroyInstance(state_);
        state_ = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
        BrotliEncoderSetParameter(state_, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(quality_));
        BrotliEncoderSetParameter(state_, BROTLI_PARAM_LGWIN, 22);
    }
}

}  // namespace titan::core
