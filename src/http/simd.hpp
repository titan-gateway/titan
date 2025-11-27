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


// Titan SIMD Accelerated String Operations
// Platform-agnostic SIMD primitives for HTTP parsing
//
// Supports:
// - x86_64: SSE2 (baseline), AVX2 (preferred)
// - ARM64: NEON (baseline)

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace titan::http::simd {

// ============================================================================
// CPU Feature Detection
// ============================================================================

enum class SIMDFeature {
    None = 0,
    SSE2 = 1 << 0,   // x86: 128-bit (16 bytes)
    AVX2 = 1 << 1,   // x86: 256-bit (32 bytes)
    NEON = 1 << 2,   // ARM: 128-bit (16 bytes)
};

inline SIMDFeature operator|(SIMDFeature a, SIMDFeature b) {
    return static_cast<SIMDFeature>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool operator&(SIMDFeature a, SIMDFeature b) {
    return (static_cast<int>(a) & static_cast<int>(b)) != 0;
}

// Runtime CPU feature detection
class CPUFeatures {
public:
    static CPUFeatures& instance() {
        static CPUFeatures features;
        return features;
    }

    bool has_sse2() const noexcept { return features_ & SIMDFeature::SSE2; }
    bool has_avx2() const noexcept { return features_ & SIMDFeature::AVX2; }
    bool has_neon() const noexcept { return features_ & SIMDFeature::NEON; }

private:
    CPUFeatures() : features_(detect_features()) {}

    SIMDFeature detect_features() noexcept {
        SIMDFeature result = SIMDFeature::None;

#if defined(__x86_64__) || defined(_M_X64)
        // x86_64 always has SSE2
        result = result | SIMDFeature::SSE2;

        // Check for AVX2 using CPUID
#if defined(__GNUC__) || defined(__clang__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            if (ebx & (1 << 5)) {  // AVX2 bit
                result = result | SIMDFeature::AVX2;
            }
        }
#endif

#elif defined(__aarch64__) || defined(_M_ARM64)
        // ARM64 always has NEON
        result = result | SIMDFeature::NEON;
#endif

        return result;
    }

    SIMDFeature features_;
};

// ============================================================================
// Platform-specific SIMD Headers
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)
    #include <emmintrin.h>  // SSE2
    #if defined(__AVX2__)
        #include <immintrin.h>  // AVX2
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #include <arm_neon.h>
#endif

// ============================================================================
// SIMD String Operations
// ============================================================================

// Find first occurrence of character in buffer
// Returns pointer to character or nullptr if not found
inline const char* find_char(const char* data, size_t len, char ch) noexcept {
    const char* ptr = data;
    const char* end = data + len;

#if defined(__AVX2__)
    if (CPUFeatures::instance().has_avx2() && len >= 32) {
        __m256i needle = _mm256_set1_epi8(ch);

        // Process 32 bytes at a time
        while (ptr + 32 <= end) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
            __m256i cmp = _mm256_cmpeq_epi8(chunk, needle);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0) {
                // Found match - determine exact position
                int offset = __builtin_ctz(mask);
                return ptr + offset;
            }
            ptr += 32;
        }
    }
#elif defined(__SSE2__)
    if (CPUFeatures::instance().has_sse2() && len >= 16) {
        __m128i needle = _mm_set1_epi8(ch);

        // Process 16 bytes at a time
        while (ptr + 16 <= end) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
            __m128i cmp = _mm_cmpeq_epi8(chunk, needle);
            int mask = _mm_movemask_epi8(cmp);

            if (mask != 0) {
                int offset = __builtin_ctz(mask);
                return ptr + offset;
            }
            ptr += 16;
        }
    }
#elif defined(__aarch64__)
    if (CPUFeatures::instance().has_neon() && len >= 16) {
        uint8x16_t needle = vdupq_n_u8(static_cast<uint8_t>(ch));

        // Process 16 bytes at a time
        while (ptr + 16 <= end) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr));
            uint8x16_t cmp = vceqq_u8(chunk, needle);

            // Check if any lane matched
            uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
            if (vgetq_lane_u64(cmp64, 0) != 0 || vgetq_lane_u64(cmp64, 1) != 0) {
                // Found match - scan to find exact position
                for (int i = 0; i < 16; i++) {
                    if (ptr[i] == ch) return ptr + i;
                }
            }
            ptr += 16;
        }
    }
#endif

    // Scalar fallback for remaining bytes
    while (ptr < end) {
        if (*ptr == ch) return ptr;
        ptr++;
    }

    return nullptr;
}

// Find CRLF (\r\n) sequence in buffer
// Returns pointer to \r or nullptr if not found
inline const char* find_crlf(const char* data, size_t len) noexcept {
    if (len < 2) return nullptr;

    const char* ptr = data;
    const char* end = data + len - 1;  // -1 because we need space for \n

#if defined(__AVX2__)
    if (CPUFeatures::instance().has_avx2() && len >= 32) {
        __m256i cr = _mm256_set1_epi8('\r');

        while (ptr + 32 <= end) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
            __m256i cmp = _mm256_cmpeq_epi8(chunk, cr);
            int mask = _mm256_movemask_epi8(cmp);

            while (mask != 0) {
                int offset = __builtin_ctz(mask);
                if (ptr[offset + 1] == '\n') {
                    return ptr + offset;
                }
                mask &= (mask - 1);  // Clear lowest bit
            }
            ptr += 32;
        }
    }
#elif defined(__SSE2__)
    if (CPUFeatures::instance().has_sse2() && len >= 16) {
        __m128i cr = _mm_set1_epi8('\r');

        while (ptr + 16 <= end) {
            __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
            __m128i cmp = _mm_cmpeq_epi8(chunk, cr);
            int mask = _mm_movemask_epi8(cmp);

            while (mask != 0) {
                int offset = __builtin_ctz(mask);
                if (ptr[offset + 1] == '\n') {
                    return ptr + offset;
                }
                mask &= (mask - 1);
            }
            ptr += 16;
        }
    }
#elif defined(__aarch64__)
    if (CPUFeatures::instance().has_neon() && len >= 16) {
        uint8x16_t cr = vdupq_n_u8('\r');

        while (ptr + 16 <= end) {
            uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr));
            uint8x16_t cmp = vceqq_u8(chunk, cr);

            uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
            if (vgetq_lane_u64(cmp64, 0) != 0 || vgetq_lane_u64(cmp64, 1) != 0) {
                for (int i = 0; i < 16 && ptr + i < end; i++) {
                    if (ptr[i] == '\r' && ptr[i + 1] == '\n') {
                        return ptr + i;
                    }
                }
            }
            ptr += 16;
        }
    }
#endif

    // Scalar fallback
    while (ptr < end) {
        if (*ptr == '\r' && *(ptr + 1) == '\n') {
            return ptr;
        }
        ptr++;
    }

    return nullptr;
}

// Convert ASCII character to lowercase
inline char to_lower(char c) noexcept {
    // Branchless: set bit 5 if c is uppercase (A-Z)
    return c | (((c >= 'A') & (c <= 'Z')) << 5);
}

// Case-insensitive memory comparison
// Returns 0 if equal, <0 if a < b, >0 if a > b
inline int memcmp_case_insensitive(const char* a, const char* b, size_t len) noexcept {
    const char* pa = a;
    const char* pb = b;
    const char* end = a + len;

#if defined(__AVX2__)
    if (CPUFeatures::instance().has_avx2() && len >= 32) {
        __m256i a_z_mask = _mm256_set1_epi8(0x20);  // Bit 5
        __m256i lower_a = _mm256_set1_epi8('A' - 1);
        __m256i upper_z = _mm256_set1_epi8('Z' + 1);

        while (pa + 32 <= end) {
            __m256i chunk_a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pa));
            __m256i chunk_b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pb));

            // Convert to lowercase: set bit 5 for A-Z
            __m256i is_upper_a = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk_a, lower_a),
                _mm256_cmpgt_epi8(upper_z, chunk_a)
            );
            __m256i lower_chunk_a = _mm256_or_si256(chunk_a, _mm256_and_si256(is_upper_a, a_z_mask));

            __m256i is_upper_b = _mm256_and_si256(
                _mm256_cmpgt_epi8(chunk_b, lower_a),
                _mm256_cmpgt_epi8(upper_z, chunk_b)
            );
            __m256i lower_chunk_b = _mm256_or_si256(chunk_b, _mm256_and_si256(is_upper_b, a_z_mask));

            // Compare
            __m256i cmp = _mm256_cmpeq_epi8(lower_chunk_a, lower_chunk_b);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0xFFFFFFFF) {
                // Found difference - find first mismatch for proper comparison result
                for (size_t i = 0; i < 32; i++) {
                    char ca = to_lower(pa[i]);
                    char cb = to_lower(pb[i]);
                    if (ca != cb) return ca - cb;
                }
            }

            pa += 32;
            pb += 32;
        }
    }
#elif defined(__SSE2__)
    if (CPUFeatures::instance().has_sse2() && len >= 16) {
        __m128i a_z_mask = _mm_set1_epi8(0x20);
        __m128i lower_a = _mm_set1_epi8('A' - 1);
        __m128i upper_z = _mm_set1_epi8('Z' + 1);

        while (pa + 16 <= end) {
            __m128i chunk_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pa));
            __m128i chunk_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pb));

            __m128i is_upper_a = _mm_and_si128(
                _mm_cmpgt_epi8(chunk_a, lower_a),
                _mm_cmpgt_epi8(upper_z, chunk_a)
            );
            __m128i lower_chunk_a = _mm_or_si128(chunk_a, _mm_and_si128(is_upper_a, a_z_mask));

            __m128i is_upper_b = _mm_and_si128(
                _mm_cmpgt_epi8(chunk_b, lower_a),
                _mm_cmpgt_epi8(upper_z, chunk_b)
            );
            __m128i lower_chunk_b = _mm_or_si128(chunk_b, _mm_and_si128(is_upper_b, a_z_mask));

            __m128i cmp = _mm_cmpeq_epi8(lower_chunk_a, lower_chunk_b);
            int mask = _mm_movemask_epi8(cmp);

            if (mask != 0xFFFF) {
                for (size_t i = 0; i < 16; i++) {
                    char ca = to_lower(pa[i]);
                    char cb = to_lower(pb[i]);
                    if (ca != cb) return ca - cb;
                }
            }

            pa += 16;
            pb += 16;
        }
    }
#endif

    // Scalar fallback for remaining bytes
    while (pa < end) {
        char ca = to_lower(*pa);
        char cb = to_lower(*pb);
        if (ca != cb) return ca - cb;
        pa++;
        pb++;
    }

    return 0;
}

// Case-insensitive string equality check
inline bool strcasecmp_eq(const char* a, const char* b, size_t len) noexcept {
    return memcmp_case_insensitive(a, b, len) == 0;
}

// Find common prefix length between two strings
// Returns number of matching bytes from the start
inline size_t common_prefix_length(const char* a, const char* b, size_t len) noexcept {
    size_t i = 0;

    // For very short strings, use scalar (SIMD overhead not worth it)
    if (len < 16) {
        while (i < len && a[i] == b[i]) {
            ++i;
        }
        return i;
    }

    // Use SIMD for longer strings
    const size_t simd_limit = len - (len % 16);

#if defined(__AVX2__)
    if (CPUFeatures::instance().has_avx2() && len >= 32) {
        const size_t avx_limit = len - (len % 32);
        while (i < avx_limit) {
            __m256i chunk_a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
            __m256i chunk_b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i));
            __m256i cmp = _mm256_cmpeq_epi8(chunk_a, chunk_b);
            int mask = _mm256_movemask_epi8(cmp);

            if (mask != 0xFFFFFFFF) {
                // Found mismatch - find exact position
                for (size_t j = 0; j < 32; ++j) {
                    if (a[i + j] != b[i + j]) {
                        return i + j;
                    }
                }
            }
            i += 32;
        }
    }
#elif defined(__SSE2__)
    if (CPUFeatures::instance().has_sse2()) {
        while (i < simd_limit) {
            __m128i chunk_a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
            __m128i chunk_b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i));
            __m128i cmp = _mm_cmpeq_epi8(chunk_a, chunk_b);
            int mask = _mm_movemask_epi8(cmp);

            if (mask != 0xFFFF) {
                // Found mismatch - find exact position
                for (size_t j = 0; j < 16; ++j) {
                    if (a[i + j] != b[i + j]) {
                        return i + j;
                    }
                }
            }
            i += 16;
        }
    }
#elif defined(__aarch64__)
    if (CPUFeatures::instance().has_neon()) {
        while (i < simd_limit) {
            uint8x16_t chunk_a = vld1q_u8(reinterpret_cast<const uint8_t*>(a + i));
            uint8x16_t chunk_b = vld1q_u8(reinterpret_cast<const uint8_t*>(b + i));
            uint8x16_t cmp = vceqq_u8(chunk_a, chunk_b);

            // Check if all bytes matched
            uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
            if (vgetq_lane_u64(cmp64, 0) != 0xFFFFFFFFFFFFFFFFULL ||
                vgetq_lane_u64(cmp64, 1) != 0xFFFFFFFFFFFFFFFFULL) {
                // Found mismatch - find exact position
                for (size_t j = 0; j < 16; ++j) {
                    if (a[i + j] != b[i + j]) {
                        return i + j;
                    }
                }
            }
            i += 16;
        }
    }
#endif

    // Scalar fallback for remaining bytes
    while (i < len && a[i] == b[i]) {
        ++i;
    }

    return i;
}

} // namespace titan::http::simd
