// Titan SIMD Tests
// Unit tests for SIMD-accelerated string operations

#include "../../src/http/simd.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <cstring>
#include <string>

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
        const char* headers[] = {
            "content-type",
            "Content-Type",
            "CONTENT-TYPE",
            "CoNtEnT-TyPe"
        };

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
