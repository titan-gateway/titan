// Titan SIMD Tests
// Unit tests for SIMD-accelerated string operations

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <string>

#include "../../src/http/simd.hpp"

using namespace titan::http::simd;

// ============================
// CPU Feature Detection
// ============================

TEST_CASE("CPU feature detection", "[simd][cpu]") {
    auto& cpu = CPUFeatures::instance();

    SECTION("CPU has at least one feature") {
        // Every platform should have at least one SIMD feature
        bool has_any = cpu.has_sse2() || cpu.has_avx2() || cpu.has_neon();
        REQUIRE(has_any);
    }

#if defined(__x86_64__) || defined(_M_X64)
    SECTION("x86_64 always has SSE2") {
        REQUIRE(cpu.has_sse2());
    }
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    SECTION("ARM64 always has NEON") {
        REQUIRE(cpu.has_neon());
    }
#endif
}

// ============================
// find_char() - Character Search
// ============================

TEST_CASE("find_char - Basic character search", "[simd][find_char]") {
    SECTION("Find character at beginning") {
        const char* data = "Hello, World!";
        const char* result = find_char(data, 13, 'H');
        REQUIRE(result == data);
    }

    SECTION("Find character in middle") {
        const char* data = "Hello, World!";
        const char* result = find_char(data, 13, 'W');
        REQUIRE(result == data + 7);
    }

    SECTION("Find character at end") {
        const char* data = "Hello, World!";
        const char* result = find_char(data, 13, '!');
        REQUIRE(result == data + 12);
    }

    SECTION("Character not found") {
        const char* data = "Hello, World!";
        const char* result = find_char(data, 13, 'X');
        REQUIRE(result == nullptr);
    }

    SECTION("Empty string") {
        const char* data = "";
        const char* result = find_char(data, 0, 'H');
        REQUIRE(result == nullptr);
    }

    SECTION("Long string - SIMD path") {
        // 64 bytes - should trigger SIMD code path
        const char* data = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOP";
        const char* result = find_char(data, 64, 'X');
        REQUIRE(result == data + 33);
    }

    SECTION("Find first of multiple occurrences") {
        const char* data = "banana";
        const char* result = find_char(data, 6, 'a');
        REQUIRE(result == data + 1);  // First 'a'
    }
}

// ============================
// find_crlf() - CRLF Search
// ============================

TEST_CASE("find_crlf - CRLF sequence detection", "[simd][find_crlf]") {
    SECTION("Find CRLF at beginning") {
        const char* data = "\r\nHello";
        const char* result = find_crlf(data, 7);
        REQUIRE(result == data);
    }

    SECTION("Find CRLF in HTTP header") {
        const char* data = "Host: localhost\r\nConnection: keep-alive\r\n";
        const char* result = find_crlf(data, std::strlen(data));
        REQUIRE(result == data + 15);
    }

    SECTION("CRLF not found") {
        const char* data = "No CRLF here";
        const char* result = find_crlf(data, std::strlen(data));
        REQUIRE(result == nullptr);
    }

    SECTION("Only CR without LF") {
        const char* data = "Hello\rWorld";
        const char* result = find_crlf(data, std::strlen(data));
        REQUIRE(result == nullptr);
    }

    SECTION("Only LF without CR") {
        const char* data = "Hello\nWorld";
        const char* result = find_crlf(data, std::strlen(data));
        REQUIRE(result == nullptr);
    }

    SECTION("String too short") {
        const char* data = "\r";
        const char* result = find_crlf(data, 1);
        REQUIRE(result == nullptr);
    }

    SECTION("Empty string") {
        const char* data = "";
        const char* result = find_crlf(data, 0);
        REQUIRE(result == nullptr);
    }

    SECTION("Multiple CRLF - find first") {
        const char* data = "Line1\r\nLine2\r\nLine3\r\n";
        const char* result = find_crlf(data, std::strlen(data));
        REQUIRE(result == data + 5);
    }

    SECTION("Long string with CRLF - SIMD path") {
        // 100 bytes with CRLF at position 50
        std::string data(50, 'A');
        data += "\r\n";
        data.append(48, 'B');

        const char* result = find_crlf(data.c_str(), data.size());
        REQUIRE(result == data.c_str() + 50);
    }

    SECTION("CRLF at end of long string") {
        std::string data(62, 'X');
        data += "\r\n";

        const char* result = find_crlf(data.c_str(), data.size());
        REQUIRE(result == data.c_str() + 62);
    }
}

// ============================
// to_lower() - Character Case Conversion
// ============================

TEST_CASE("to_lower - Character case conversion", "[simd][to_lower]") {
    SECTION("Uppercase to lowercase") {
        REQUIRE(to_lower('A') == 'a');
        REQUIRE(to_lower('Z') == 'z');
        REQUIRE(to_lower('M') == 'm');
    }

    SECTION("Lowercase unchanged") {
        REQUIRE(to_lower('a') == 'a');
        REQUIRE(to_lower('z') == 'z');
        REQUIRE(to_lower('m') == 'm');
    }

    SECTION("Non-alphabetic unchanged") {
        REQUIRE(to_lower('0') == '0');
        REQUIRE(to_lower('9') == '9');
        REQUIRE(to_lower('-') == '-');
        REQUIRE(to_lower('_') == '_');
        REQUIRE(to_lower(' ') == ' ');
    }
}

// ============================
// memcmp_case_insensitive() - String Comparison
// ============================

TEST_CASE("memcmp_case_insensitive - Case-insensitive comparison", "[simd][memcmp]") {
    SECTION("Equal strings - same case") {
        const char* a = "hello";
        const char* b = "hello";
        REQUIRE(memcmp_case_insensitive(a, b, 5) == 0);
    }

    SECTION("Equal strings - different case") {
        const char* a = "Hello";
        const char* b = "hello";
        REQUIRE(memcmp_case_insensitive(a, b, 5) == 0);
    }

    SECTION("Equal strings - mixed case") {
        const char* a = "CoNtEnT-TyPe";
        const char* b = "content-type";
        REQUIRE(memcmp_case_insensitive(a, b, 12) == 0);
    }

    SECTION("Different strings - a < b") {
        const char* a = "apple";
        const char* b = "banana";
        REQUIRE(memcmp_case_insensitive(a, b, 5) < 0);
    }

    SECTION("Different strings - a > b") {
        const char* a = "zebra";
        const char* b = "apple";
        REQUIRE(memcmp_case_insensitive(a, b, 5) > 0);
    }

    SECTION("HTTP headers - Content-Type") {
        const char* a = "Content-Type";
        const char* b = "content-type";
        REQUIRE(memcmp_case_insensitive(a, b, 12) == 0);
    }

    SECTION("HTTP headers - Authorization") {
        const char* a = "AUTHORIZATION";
        const char* b = "authorization";
        REQUIRE(memcmp_case_insensitive(a, b, 13) == 0);
    }

    SECTION("Long string - SIMD path") {
        // 64 bytes - should trigger SIMD
        const char* a = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQR";
        const char* b = "0123456789abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqr";
        REQUIRE(memcmp_case_insensitive(a, b, 64) == 0);
    }

    SECTION("Empty strings") {
        const char* a = "";
        const char* b = "";
        REQUIRE(memcmp_case_insensitive(a, b, 0) == 0);
    }

    SECTION("Strings with numbers and special chars") {
        const char* a = "HTTP/1.1";
        const char* b = "http/1.1";
        REQUIRE(memcmp_case_insensitive(a, b, 8) == 0);
    }
}

// ============================
// strcasecmp_eq() - Equality Check
// ============================

TEST_CASE("strcasecmp_eq - Case-insensitive equality", "[simd][strcasecmp_eq]") {
    SECTION("Equal strings") {
        const char* a = "Content-Type";
        const char* b = "content-type";
        REQUIRE(strcasecmp_eq(a, b, 12));
    }

    SECTION("Different strings") {
        const char* a = "Content-Type";
        const char* b = "Content-Leng";
        REQUIRE_FALSE(strcasecmp_eq(a, b, 12));
    }

    SECTION("Common HTTP headers") {
        REQUIRE(strcasecmp_eq("host", "HOST", 4));
        REQUIRE(strcasecmp_eq("Accept", "accept", 6));
        REQUIRE(strcasecmp_eq("User-Agent", "user-agent", 10));
    }
}

// ============================
// Real-world HTTP Parsing Scenarios
// ============================

TEST_CASE("SIMD operations - HTTP parsing scenarios", "[simd][http]") {
    SECTION("Parse HTTP request line") {
        const char* request = "GET /api/users HTTP/1.1\r\nHost: localhost\r\n\r\n";

        // Find first CRLF (end of request line)
        const char* first_crlf = find_crlf(request, std::strlen(request));
        REQUIRE(first_crlf != nullptr);
        REQUIRE(first_crlf - request == 23);
    }

    SECTION("Find header separator") {
        const char* headers = "Content-Type: application/json\r\n\r\n";

        // Find double CRLF (end of headers)
        const char* first = find_crlf(headers, std::strlen(headers));
        REQUIRE(first != nullptr);

        const char* second = find_crlf(first + 2, std::strlen(first + 2));
        REQUIRE(second == first + 2);
    }

    SECTION("Case-insensitive header matching") {
        const char* headers[] = {"content-type", "Content-Type", "CONTENT-TYPE", "CoNtEnT-TyPe"};

        for (const char* header : headers) {
            REQUIRE(strcasecmp_eq(header, "content-type", 12));
        }
    }
}

// ============================
// Edge Cases and Boundary Conditions
// ============================

TEST_CASE("SIMD operations - Edge cases", "[simd][edge]") {
    SECTION("find_char - Single character string") {
        const char* data = "X";
        REQUIRE(find_char(data, 1, 'X') == data);
        REQUIRE(find_char(data, 1, 'Y') == nullptr);
    }

    SECTION("find_crlf - Minimal valid input") {
        const char* data = "\r\n";
        REQUIRE(find_crlf(data, 2) == data);
    }

    SECTION("memcmp_case_insensitive - Single character") {
        REQUIRE(memcmp_case_insensitive("A", "a", 1) == 0);
        REQUIRE(memcmp_case_insensitive("A", "B", 1) < 0);
    }

    SECTION("find_char - String with null bytes") {
        const char data[] = "Hello\0World";
        // Should only search up to specified length
        REQUIRE(find_char(data, 11, 'W') == data + 6);
    }

    SECTION("Large string - Stress test SIMD path") {
        // 256 bytes
        std::string large(200, 'A');
        large += "NEEDLE";
        large.append(50, 'B');

        const char* result = find_char(large.c_str(), large.size(), 'N');
        REQUIRE(result == large.c_str() + 200);
    }

    SECTION("find_crlf - CR at end without LF") {
        const char* data = "Hello\r";
        REQUIRE(find_crlf(data, 6) == nullptr);
    }
}

// ============================
// unmask_payload() - WebSocket Frame Unmasking
// ============================

TEST_CASE("unmask_payload - Basic unmasking", "[simd][websocket]") {
    SECTION("Small payload (5 bytes)") {
        // Masked "Hello" with key 0x12345678
        std::vector<uint8_t> payload = {0x5a, 0x51, 0x3a, 0x14, 0x7d};
        uint32_t masking_key = 0x12345678;

        unmask_payload(payload.data(), payload.size(), masking_key);

        // Should be "Hello"
        REQUIRE(payload[0] == 'H');
        REQUIRE(payload[1] == 'e');
        REQUIRE(payload[2] == 'l');
        REQUIRE(payload[3] == 'l');
        REQUIRE(payload[4] == 'o');
    }

    SECTION("Empty payload") {
        std::vector<uint8_t> payload;
        unmask_payload(payload.data(), 0, 0x12345678);
        // Should not crash
    }

    SECTION("Single byte") {
        std::vector<uint8_t> payload = {0xFF};
        unmask_payload(payload.data(), 1, 0x12000000);
        REQUIRE(payload[0] == (0xFF ^ 0x12));
    }

    SECTION("Unmasking is reversible") {
        std::vector<uint8_t> original = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        std::vector<uint8_t> payload = original;
        uint32_t key = 0xDEADBEEF;

        // Mask
        unmask_payload(payload.data(), payload.size(), key);
        REQUIRE(payload != original);  // Should be different

        // Unmask (XOR is self-inverse)
        unmask_payload(payload.data(), payload.size(), key);
        REQUIRE(payload == original);  // Should be back to original
    }
}

TEST_CASE("unmask_payload - SIMD vs scalar correctness", "[simd][websocket]") {
    // Test that SIMD and scalar produce identical results for various sizes

    auto test_size = [](size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i & 0xFF);
        }

        std::vector<uint8_t> simd_result = data;
        std::vector<uint8_t> scalar_result = data;

        uint32_t key = 0xCAFEBABE;

        // SIMD path
        unmask_payload(simd_result.data(), simd_result.size(), key);

        // Scalar path (simulate by unmasking byte-by-byte)
        uint8_t key_bytes[4] = {
            static_cast<uint8_t>(key >> 24),
            static_cast<uint8_t>(key >> 16),
            static_cast<uint8_t>(key >> 8),
            static_cast<uint8_t>(key)
        };
        for (size_t i = 0; i < scalar_result.size(); ++i) {
            scalar_result[i] ^= key_bytes[i % 4];
        }

        REQUIRE(simd_result == scalar_result);
    };

    SECTION("15 bytes (SSE2/NEON boundary - 1)") { test_size(15); }
    SECTION("16 bytes (SSE2/NEON boundary)") { test_size(16); }
    SECTION("17 bytes (SSE2/NEON boundary + 1)") { test_size(17); }
    SECTION("31 bytes (AVX2 boundary - 1)") { test_size(31); }
    SECTION("32 bytes (AVX2 boundary)") { test_size(32); }
    SECTION("33 bytes (AVX2 boundary + 1)") { test_size(33); }
    SECTION("100 bytes") { test_size(100); }
    SECTION("1000 bytes") { test_size(1000); }
    SECTION("1MB payload") { test_size(1024 * 1024); }
}

TEST_CASE("unmask_payload - Edge cases", "[simd][websocket]") {
    SECTION("All zeros") {
        std::vector<uint8_t> payload(100, 0x00);
        unmask_payload(payload.data(), payload.size(), 0xFFFFFFFF);

        // All zeros XOR 0xFF = 0xFF pattern
        for (size_t i = 0; i < payload.size(); ++i) {
            REQUIRE(payload[i] == 0xFF);
        }
    }

    SECTION("All ones") {
        std::vector<uint8_t> payload(100, 0xFF);
        unmask_payload(payload.data(), payload.size(), 0x00000000);

        // All ones XOR 0x00 = 0xFF unchanged
        for (size_t i = 0; i < payload.size(); ++i) {
            REQUIRE(payload[i] == 0xFF);
        }
    }

    SECTION("Masking key with all zeros") {
        std::vector<uint8_t> payload = {0x12, 0x34, 0x56, 0x78, 0x9A};
        std::vector<uint8_t> original = payload;

        unmask_payload(payload.data(), payload.size(), 0x00000000);

        // XOR with zero = no change
        REQUIRE(payload == original);
    }
}

TEST_CASE("unmask_payload - Performance validation", "[simd][websocket][!benchmark]") {
    const size_t PAYLOAD_SIZE = 1024 * 1024;  // 1MB
    std::vector<uint8_t> payload(PAYLOAD_SIZE);

    for (size_t i = 0; i < PAYLOAD_SIZE; ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    uint32_t key = 0x12345678;

    BENCHMARK("SIMD unmask 1MB") {
        unmask_payload(payload.data(), payload.size(), key);
        return payload[0];  // Prevent optimization
    };
}
