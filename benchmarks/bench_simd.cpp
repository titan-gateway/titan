// Titan SIMD Benchmark
// Measures performance of SIMD operations vs scalar implementations

#include "../src/http/simd.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>

using namespace titan::http::simd;

// Scalar reference implementations for comparison
namespace scalar {

const char* find_char(const char* data, size_t len, char ch) noexcept {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == ch) return &data[i];
    }
    return nullptr;
}

const char* find_crlf(const char* data, size_t len) noexcept {
    if (len < 2) return nullptr;
    for (size_t i = 0; i < len - 1; i++) {
        if (data[i] == '\r' && data[i+1] == '\n') return &data[i];
    }
    return nullptr;
}

char to_lower(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int memcmp_case_insensitive(const char* a, const char* b, size_t len) noexcept {
    for (size_t i = 0; i < len; i++) {
        char ca = to_lower(a[i]);
        char cb = to_lower(b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

} // namespace scalar

// Benchmark helper
template<typename Func>
double benchmark(const std::string& name, Func&& func, size_t iterations) {
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < iterations; i++) {
        func();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return static_cast<double>(duration.count()) / iterations;
}

// Generate random string
std::string random_string(size_t length, std::mt19937& rng) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);

    std::string str;
    str.reserve(length);
    for (size_t i = 0; i < length; i++) {
        str += charset[dist(rng)];
    }
    return str;
}

void benchmark_find_char() {
    std::cout << "\n=== find_char Benchmark ===\n";
    std::mt19937 rng(42);

    const size_t iterations = 1000000;
    std::vector<size_t> sizes = {16, 32, 64, 128, 256, 512};

    std::cout << std::setw(10) << "Size"
              << std::setw(15) << "SIMD (ns)"
              << std::setw(15) << "Scalar (ns)"
              << std::setw(15) << "Speedup" << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (size_t size : sizes) {
        std::string data = random_string(size, rng);
        char needle = data[size / 2];  // Character in the middle

        double simd_time = benchmark("SIMD", [&]() {
            volatile auto result = find_char(data.c_str(), data.size(), needle);
            (void)result;
        }, iterations);

        double scalar_time = benchmark("Scalar", [&]() {
            volatile auto result = scalar::find_char(data.c_str(), data.size(), needle);
            (void)result;
        }, iterations);

        std::cout << std::setw(10) << size
                  << std::setw(15) << std::fixed << std::setprecision(2) << simd_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << scalar_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << (scalar_time / simd_time) << "x\n";
    }
}

void benchmark_find_crlf() {
    std::cout << "\n=== find_crlf Benchmark ===\n";
    std::mt19937 rng(42);

    const size_t iterations = 1000000;
    std::vector<size_t> sizes = {32, 64, 128, 256, 512};

    std::cout << std::setw(10) << "Size"
              << std::setw(15) << "SIMD (ns)"
              << std::setw(15) << "Scalar (ns)"
              << std::setw(15) << "Speedup" << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (size_t size : sizes) {
        std::string data = random_string(size - 2, rng);
        data.insert(size / 2, "\r\n");  // CRLF in the middle

        double simd_time = benchmark("SIMD", [&]() {
            volatile auto result = find_crlf(data.c_str(), data.size());
            (void)result;
        }, iterations);

        double scalar_time = benchmark("Scalar", [&]() {
            volatile auto result = scalar::find_crlf(data.c_str(), data.size());
            (void)result;
        }, iterations);

        std::cout << std::setw(10) << size
                  << std::setw(15) << std::fixed << std::setprecision(2) << simd_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << scalar_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << (scalar_time / simd_time) << "x\n";
    }
}

void benchmark_memcmp_case_insensitive() {
    std::cout << "\n=== memcmp_case_insensitive Benchmark ===\n";
    std::mt19937 rng(42);

    const size_t iterations = 1000000;
    std::vector<size_t> sizes = {16, 32, 64, 128, 256};

    std::cout << std::setw(10) << "Size"
              << std::setw(15) << "SIMD (ns)"
              << std::setw(15) << "Scalar (ns)"
              << std::setw(15) << "Speedup" << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (size_t size : sizes) {
        std::string a = random_string(size, rng);
        std::string b = a;

        // Randomize case
        for (size_t i = 0; i < b.size(); i += 2) {
            if (b[i] >= 'a' && b[i] <= 'z') {
                b[i] = b[i] - 32;  // To uppercase
            }
        }

        double simd_time = benchmark("SIMD", [&]() {
            volatile auto result = memcmp_case_insensitive(a.c_str(), b.c_str(), a.size());
            (void)result;
        }, iterations);

        double scalar_time = benchmark("Scalar", [&]() {
            volatile auto result = scalar::memcmp_case_insensitive(a.c_str(), b.c_str(), a.size());
            (void)result;
        }, iterations);

        std::cout << std::setw(10) << size
                  << std::setw(15) << std::fixed << std::setprecision(2) << simd_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << scalar_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << (scalar_time / simd_time) << "x\n";
    }
}

void benchmark_http_headers() {
    std::cout << "\n=== HTTP Header Matching (Real-world) ===\n";

    const size_t iterations = 10000000;
    const char* headers[] = {
        "content-type",
        "content-length",
        "authorization",
        "user-agent",
        "accept",
        "accept-encoding",
        "host",
        "connection"
    };

    const char* test_headers[] = {
        "Content-Type",
        "CONTENT-LENGTH",
        "Authorization",
        "USER-AGENT",
        "Accept",
        "Accept-Encoding",
        "Host",
        "Connection"
    };

    std::cout << std::setw(20) << "Header"
              << std::setw(15) << "SIMD (ns)"
              << std::setw(15) << "Scalar (ns)"
              << std::setw(15) << "Speedup" << "\n";
    std::cout << std::string(65, '-') << "\n";

    for (size_t i = 0; i < 8; i++) {
        size_t len = std::strlen(headers[i]);

        double simd_time = benchmark("SIMD", [&]() {
            volatile auto result = strcasecmp_eq(headers[i], test_headers[i], len);
            (void)result;
        }, iterations);

        double scalar_time = benchmark("Scalar", [&]() {
            volatile auto result = (scalar::memcmp_case_insensitive(headers[i], test_headers[i], len) == 0);
            (void)result;
        }, iterations);

        std::cout << std::setw(20) << headers[i]
                  << std::setw(15) << std::fixed << std::setprecision(2) << simd_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << scalar_time
                  << std::setw(15) << std::fixed << std::setprecision(2) << (scalar_time / simd_time) << "x\n";
    }
}

int main() {
    std::cout << "Titan SIMD Performance Benchmark\n";
    std::cout << "=================================\n";

    auto& cpu = CPUFeatures::instance();
    std::cout << "\nCPU Features:\n";
    std::cout << "  SSE2: " << (cpu.has_sse2() ? "Yes" : "No") << "\n";
    std::cout << "  AVX2: " << (cpu.has_avx2() ? "Yes" : "No") << "\n";
    std::cout << "  NEON: " << (cpu.has_neon() ? "Yes" : "No") << "\n";

    benchmark_find_char();
    benchmark_find_crlf();
    benchmark_memcmp_case_insensitive();
    benchmark_http_headers();

    std::cout << "\n";
    return 0;
}
