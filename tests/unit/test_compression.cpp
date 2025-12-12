// Titan Compression Contexts Unit Tests

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include "../../src/control/config.hpp"
#include "../../src/core/compression.hpp"
#include "../../src/gateway/compression_middleware.hpp"
#include "../../src/http/http.hpp"

using namespace titan::core;
using namespace titan::control;
using namespace titan::gateway;
using namespace titan::http;

// Helper function to create test data
static std::vector<uint8_t> create_test_data(size_t size, uint8_t pattern = 0) {
    std::vector<uint8_t> data(size);
    if (pattern == 0) {
        // Compressible data (repeated pattern)
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
    } else {
        // Custom pattern
        std::fill(data.begin(), data.end(), pattern);
    }
    return data;
}

// Helper function to convert string to bytes
static std::vector<uint8_t> string_to_bytes(std::string_view str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

// ============================================================================
// Encoding Enum Tests
// ============================================================================

TEST_CASE("CompressionEncoding enum conversions", "[compression][encoding]") {
    SECTION("encoding_to_string") {
        REQUIRE(std::string_view(encoding_to_string(CompressionEncoding::GZIP)) == "gzip");
        REQUIRE(std::string_view(encoding_to_string(CompressionEncoding::ZSTD)) == "zstd");
        REQUIRE(std::string_view(encoding_to_string(CompressionEncoding::BROTLI)) == "br");
        REQUIRE(std::string_view(encoding_to_string(CompressionEncoding::NONE)) == "");
    }

    SECTION("encoding_from_string") {
        REQUIRE(encoding_from_string("gzip") == CompressionEncoding::GZIP);
        REQUIRE(encoding_from_string("zstd") == CompressionEncoding::ZSTD);
        REQUIRE(encoding_from_string("br") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("brotli") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("") == CompressionEncoding::NONE);
        REQUIRE(encoding_from_string("invalid") == CompressionEncoding::NONE);
        REQUIRE(encoding_from_string("deflate") == CompressionEncoding::NONE);
    }

    SECTION("encoding_from_string case-insensitive") {
        // Test case-insensitive matching per HTTP best practices
        REQUIRE(encoding_from_string("GZIP") == CompressionEncoding::GZIP);
        REQUIRE(encoding_from_string("Gzip") == CompressionEncoding::GZIP);
        REQUIRE(encoding_from_string("GZip") == CompressionEncoding::GZIP);
        REQUIRE(encoding_from_string("gzip") == CompressionEncoding::GZIP);

        REQUIRE(encoding_from_string("ZSTD") == CompressionEncoding::ZSTD);
        REQUIRE(encoding_from_string("Zstd") == CompressionEncoding::ZSTD);
        REQUIRE(encoding_from_string("zstd") == CompressionEncoding::ZSTD);

        REQUIRE(encoding_from_string("BR") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("Br") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("br") == CompressionEncoding::BROTLI);

        REQUIRE(encoding_from_string("BROTLI") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("Brotli") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("BrotLI") == CompressionEncoding::BROTLI);
        REQUIRE(encoding_from_string("brotli") == CompressionEncoding::BROTLI);
    }

    SECTION("Round-trip conversion") {
        auto encodings = {CompressionEncoding::GZIP, CompressionEncoding::ZSTD,
                          CompressionEncoding::BROTLI};
        for (auto encoding : encodings) {
            auto str = encoding_to_string(encoding);
            REQUIRE(encoding_from_string(str) == encoding);
        }
    }
}

// ============================================================================
// GzipContext Tests
// ============================================================================

TEST_CASE("GzipContext basic compression", "[compression][gzip]") {
    GzipContext ctx;

    SECTION("Compress text data") {
        // Use 100KB of compressible text data to see compression value
        auto input = create_test_data(100 * 1024);  // 100KB
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());  // Should be smaller
    }

    SECTION("Compress empty data returns empty") {
        auto compressed = ctx.compress(nullptr, 0);
        REQUIRE(compressed.empty());  // Empty input returns empty output
    }

    SECTION("Compress repeated pattern") {
        auto input = create_test_data(1024, 'A');  // 1KB of 'A'
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
        // Highly compressible - should achieve >90% compression
        REQUIRE(compressed.size() < input.size() / 10);
    }

    SECTION("Compress large data") {
        auto input = create_test_data(100 * 1024);  // 100KB
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
    }
}

TEST_CASE("GzipContext compression levels", "[compression][gzip]") {
    SECTION("Level 1 (fastest)") {
        GzipContext ctx(1);
        REQUIRE(ctx.level() == 1);

        auto input = create_test_data(10240);  // 10KB
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Level 6 (default)") {
        GzipContext ctx(6);
        REQUIRE(ctx.level() == 6);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Level 9 (best compression)") {
        GzipContext ctx(9);
        REQUIRE(ctx.level() == 9);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Higher level produces smaller output") {
        auto input = create_test_data(10240);

        GzipContext ctx1(1);
        auto compressed1 = ctx1.compress(input.data(), input.size());

        GzipContext ctx9(9);
        auto compressed9 = ctx9.compress(input.data(), input.size());

        // Level 9 should produce smaller or equal output
        REQUIRE(compressed9.size() <= compressed1.size());
    }
}

TEST_CASE("GzipContext reset and reuse", "[compression][gzip]") {
    GzipContext ctx;

    SECTION("Reuse after reset") {
        auto input1 = string_to_bytes("First message");
        auto compressed1 = ctx.compress(input1.data(), input1.size());
        REQUIRE(!compressed1.empty());

        ctx.reset();

        auto input2 = string_to_bytes("Second message");
        auto compressed2 = ctx.compress(input2.data(), input2.size());
        REQUIRE(!compressed2.empty());

        // Different inputs should produce different outputs
        REQUIRE(compressed1 != compressed2);
    }

    SECTION("Multiple resets") {
        for (int i = 0; i < 10; ++i) {
            auto input = create_test_data(1024 + i * 100);
            auto compressed = ctx.compress(input.data(), input.size());
            REQUIRE(!compressed.empty());
            ctx.reset();
        }
    }
}

TEST_CASE("GzipContext streaming compression", "[compression][gzip][streaming]") {
    GzipContext ctx;

    SECTION("Stream small data") {
        auto input = string_to_bytes("Hello, streaming world!");
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
    }

    SECTION("Stream large data in chunks") {
        auto input = create_test_data(50 * 1024);  // 50KB
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
        REQUIRE(output.size() < input.size());
    }
}

// ============================================================================
// ZstdContext Tests
// ============================================================================

TEST_CASE("ZstdContext basic compression", "[compression][zstd]") {
    ZstdContext ctx;

    SECTION("Compress simple text") {
        auto input = string_to_bytes("Hello, World! This is a test.");
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        // Note: For very small data, compressed might be larger due to overhead
    }

    SECTION("Compress empty data returns empty") {
        auto compressed = ctx.compress(nullptr, 0);
        REQUIRE(compressed.empty());  // Empty input returns empty output
    }

    SECTION("Compress repeated pattern") {
        auto input = create_test_data(1024, 'A');  // 1KB of 'A'
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
        // Highly compressible
        REQUIRE(compressed.size() < input.size() / 10);
    }

    SECTION("Compress large data") {
        auto input = create_test_data(100 * 1024);  // 100KB
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
    }
}

TEST_CASE("ZstdContext compression levels", "[compression][zstd]") {
    SECTION("Level 1 (fastest)") {
        ZstdContext ctx(1);
        REQUIRE(ctx.level() == 1);

        auto input = create_test_data(10240);  // 10KB
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Level 5 (default)") {
        ZstdContext ctx(5);
        REQUIRE(ctx.level() == 5);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Level 10 (higher compression)") {
        ZstdContext ctx(10);
        REQUIRE(ctx.level() == 10);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Higher level produces smaller output") {
        auto input = create_test_data(10240);

        ZstdContext ctx1(1);
        auto compressed1 = ctx1.compress(input.data(), input.size());

        ZstdContext ctx10(10);
        auto compressed10 = ctx10.compress(input.data(), input.size());

        // Level 10 should produce smaller or equal output
        REQUIRE(compressed10.size() <= compressed1.size());
    }
}

TEST_CASE("ZstdContext reset and reuse", "[compression][zstd]") {
    ZstdContext ctx;

    SECTION("Reuse after reset") {
        auto input1 = string_to_bytes("First message");
        auto compressed1 = ctx.compress(input1.data(), input1.size());
        REQUIRE(!compressed1.empty());

        ctx.reset();

        auto input2 = string_to_bytes("Second message");
        auto compressed2 = ctx.compress(input2.data(), input2.size());
        REQUIRE(!compressed2.empty());

        // Different inputs should produce different outputs
        REQUIRE(compressed1 != compressed2);
    }

    SECTION("Multiple resets") {
        for (int i = 0; i < 10; ++i) {
            auto input = create_test_data(1024 + i * 100);
            auto compressed = ctx.compress(input.data(), input.size());
            REQUIRE(!compressed.empty());
            ctx.reset();
        }
    }
}

TEST_CASE("ZstdContext streaming compression", "[compression][zstd][streaming]") {
    ZstdContext ctx;

    SECTION("Stream small data") {
        auto input = string_to_bytes("Hello, streaming world!");
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
    }

    SECTION("Stream large data in chunks") {
        auto input = create_test_data(50 * 1024);  // 50KB
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
        REQUIRE(output.size() < input.size());
    }
}

// ============================================================================
// BrotliContext Tests
// ============================================================================

TEST_CASE("BrotliContext basic compression", "[compression][brotli]") {
    BrotliContext ctx;

    SECTION("Compress simple text") {
        auto input = string_to_bytes("Hello, World! This is a test.");
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
    }

    SECTION("Compress empty data returns empty") {
        auto compressed = ctx.compress(nullptr, 0);
        REQUIRE(compressed.empty());  // Empty input returns empty output
    }

    SECTION("Compress repeated pattern") {
        auto input = create_test_data(1024, 'A');  // 1KB of 'A'
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
        // Highly compressible
        REQUIRE(compressed.size() < input.size() / 10);
    }

    SECTION("Compress large data") {
        auto input = create_test_data(100 * 1024);  // 100KB
        auto compressed = ctx.compress(input.data(), input.size());

        REQUIRE(!compressed.empty());
        REQUIRE(compressed.size() < input.size());
    }
}

TEST_CASE("BrotliContext quality levels", "[compression][brotli]") {
    SECTION("Quality 0 (fastest)") {
        BrotliContext ctx(0);
        REQUIRE(ctx.quality() == 0);

        auto input = create_test_data(10240);  // 10KB
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Quality 4 (default)") {
        BrotliContext ctx(4);
        REQUIRE(ctx.quality() == 4);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Quality 11 (best compression)") {
        BrotliContext ctx(11);
        REQUIRE(ctx.quality() == 11);

        auto input = create_test_data(10240);
        auto compressed = ctx.compress(input.data(), input.size());
        REQUIRE(!compressed.empty());
    }

    SECTION("Higher quality produces smaller output") {
        auto input = create_test_data(10240);

        BrotliContext ctx0(0);
        auto compressed0 = ctx0.compress(input.data(), input.size());

        BrotliContext ctx11(11);
        auto compressed11 = ctx11.compress(input.data(), input.size());

        // Quality 11 should produce smaller or equal output
        REQUIRE(compressed11.size() <= compressed0.size());
    }
}

TEST_CASE("BrotliContext reset and reuse", "[compression][brotli]") {
    BrotliContext ctx;

    SECTION("Reuse after reset") {
        auto input1 = string_to_bytes("First message");
        auto compressed1 = ctx.compress(input1.data(), input1.size());
        REQUIRE(!compressed1.empty());

        ctx.reset();

        auto input2 = string_to_bytes("Second message");
        auto compressed2 = ctx.compress(input2.data(), input2.size());
        REQUIRE(!compressed2.empty());

        // Different inputs should produce different outputs
        REQUIRE(compressed1 != compressed2);
    }

    SECTION("Multiple resets") {
        for (int i = 0; i < 10; ++i) {
            auto input = create_test_data(1024 + i * 100);
            auto compressed = ctx.compress(input.data(), input.size());
            REQUIRE(!compressed.empty());
            ctx.reset();
        }
    }
}

TEST_CASE("BrotliContext streaming compression", "[compression][brotli][streaming]") {
    BrotliContext ctx;

    SECTION("Stream small data") {
        auto input = string_to_bytes("Hello, streaming world!");
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
    }

    SECTION("Stream large data in chunks") {
        auto input = create_test_data(50 * 1024);  // 50KB
        std::vector<uint8_t> output;

        bool success = ctx.compress_stream(
            input.data(), input.size(),
            [&output](const uint8_t* data, size_t size) {
                output.insert(output.end(), data, data + size);
            },
            true);

        REQUIRE(success);
        REQUIRE(!output.empty());
        REQUIRE(output.size() < input.size());
    }
}

// ============================================================================
// Comparative Tests (Algorithm Comparison)
// ============================================================================

TEST_CASE("Compare compression algorithms", "[compression][comparison]") {
    auto input = create_test_data(10240);  // 10KB compressible data

    SECTION("All algorithms produce compressed output") {
        GzipContext gzip_ctx;
        ZstdContext zstd_ctx;
        BrotliContext brotli_ctx;

        auto gzip_out = gzip_ctx.compress(input.data(), input.size());
        auto zstd_out = zstd_ctx.compress(input.data(), input.size());
        auto brotli_out = brotli_ctx.compress(input.data(), input.size());

        REQUIRE(!gzip_out.empty());
        REQUIRE(!zstd_out.empty());
        REQUIRE(!brotli_out.empty());

        // All should compress the data
        REQUIRE(gzip_out.size() < input.size());
        REQUIRE(zstd_out.size() < input.size());
        REQUIRE(brotli_out.size() < input.size());
    }

    SECTION("Different algorithms produce different outputs") {
        GzipContext gzip_ctx;
        ZstdContext zstd_ctx;
        BrotliContext brotli_ctx;

        auto gzip_out = gzip_ctx.compress(input.data(), input.size());
        auto zstd_out = zstd_ctx.compress(input.data(), input.size());
        auto brotli_out = brotli_ctx.compress(input.data(), input.size());

        // Different compression formats should produce different sizes or content
        // Check first few bytes differ (compression format headers)
        bool gzip_vs_zstd_differ = (gzip_out.size() != zstd_out.size()) ||
                                   (gzip_out.size() >= 4 && zstd_out.size() >= 4 &&
                                    memcmp(gzip_out.data(), zstd_out.data(), 4) != 0);
        bool gzip_vs_brotli_differ = (gzip_out.size() != brotli_out.size()) ||
                                     (gzip_out.size() >= 4 && brotli_out.size() >= 4 &&
                                      memcmp(gzip_out.data(), brotli_out.data(), 4) != 0);
        bool zstd_vs_brotli_differ = (zstd_out.size() != brotli_out.size()) ||
                                     (zstd_out.size() >= 4 && brotli_out.size() >= 4 &&
                                      memcmp(zstd_out.data(), brotli_out.data(), 4) != 0);

        REQUIRE(gzip_vs_zstd_differ);
        REQUIRE(gzip_vs_brotli_differ);
        REQUIRE(zstd_vs_brotli_differ);
    }
}

// ============================================================================
// CompressionMiddleware Tests
// ============================================================================

// Helper to create test config
static CompressionConfig create_test_config() {
    CompressionConfig config;
    config.enabled = true;
    config.algorithms = {"zstd", "gzip", "br"};
    config.min_size = 1024;  // 1KB minimum
    config.levels.gzip = 6;
    config.levels.zstd = 5;
    config.levels.brotli = 4;
    config.content_types = {"text/plain", "text/html", "application/json", "application/xml"};
    config.excluded_content_types = {"image/", "video/", "audio/"};
    config.disable_for_paths = {};
    config.disable_when_setting_cookies = false;
    config.streaming_threshold = 1024 * 1024;  // 1MB
    return config;
}

TEST_CASE("CompressionMiddleware - Basic functionality", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Middleware has correct name") {
        REQUIRE(middleware.name() == "CompressionMiddleware");
    }

    SECTION("Compress response with gzip") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");

        // Store body data in Response's body_storage so it stays alive
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
        REQUIRE(res.get_header("Vary").find("Accept-Encoding") != std::string_view::npos);
    }

    SECTION("Compress response with zstd") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "zstd"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "zstd");
    }

    SECTION("Compress response with brotli") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "br"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "br");
    }
}

TEST_CASE("CompressionMiddleware - Accept-Encoding negotiation", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Prefer first algorithm in config when quality equal") {
        // Config has: zstd, gzip, br
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip, zstd, br"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Content-Encoding") == "zstd");
    }

    SECTION("Respect quality values") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip;q=1.0, zstd;q=0.5"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("No compression when no Accept-Encoding") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);  // No compression
        REQUIRE(res.get_header("Content-Encoding").empty());
        // Should still add Vary header
        REQUIRE(res.get_header("Vary").find("Accept-Encoding") != std::string_view::npos);
    }

    SECTION("No compression when unsupported encoding") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "deflate"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
    }

    SECTION("Case-insensitive Accept-Encoding header name") {
        // Test that header name lookup is case-insensitive per HTTP spec
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        // Use lowercase "accept-encoding" (real HTTP parsers may normalize)
        req.headers.push_back({"accept-encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Case-insensitive Accept-Encoding header value") {
        // Test that encoding values are case-insensitive per HTTP best practices
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        // Use uppercase "GZIP" in value
        req.headers.push_back({"Accept-Encoding", "GZIP"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Case-insensitive mixed encodings") {
        // Test mixed case in multiple encodings
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "GZIP, Zstd, BR"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        // Should prefer zstd (first in config) since all have equal quality
        REQUIRE(res.get_header("Content-Encoding") == "zstd");
    }
}

TEST_CASE("CompressionMiddleware - Content-Type filtering", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Compress text/plain") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain; charset=utf-8");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
    }

    SECTION("Compress application/json") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
    }

    SECTION("Skip image/png (excluded type)") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "image/png");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);  // No compression
        REQUIRE(ctx.get_metadata("compression_skip_reason") == "uncompressible_type");
    }

    SECTION("Skip video/mp4 (excluded type)") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "video/mp4");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
    }

    SECTION("Skip when no Content-Type header") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
    }
}

TEST_CASE("CompressionMiddleware - Minimum size threshold", "[compression][middleware]") {
    auto config = create_test_config();
    config.min_size = 2048;  // 2KB minimum
    CompressionMiddleware middleware(config);

    SECTION("Skip small responses") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(1024);  // 1KB
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
        REQUIRE(ctx.get_metadata("compression_skip_reason") == "too_small");
    }

    SECTION("Compress large enough responses") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(4096);  // 4KB
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
    }
}

TEST_CASE("CompressionMiddleware - Already compressed", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Skip when already gzip compressed") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("Content-Encoding", "gzip");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
        REQUIRE(ctx.get_metadata("compression_skip_reason") == "already_compressed");
    }

    SECTION("Skip when Content-Encoding is br") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("Content-Encoding", "br");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
    }

    SECTION("Compress when Content-Encoding is identity") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("Content-Encoding", "identity");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
    }
}

TEST_CASE("CompressionMiddleware - Header updates", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Add Content-Encoding header") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Add Vary header") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        auto vary = res.get_header("Vary");
        REQUIRE(vary.find("Accept-Encoding") != std::string_view::npos);
    }

    SECTION("Append to existing Vary header") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("Vary", "Origin");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        auto vary = res.get_header("Vary");
        REQUIRE(vary.find("Origin") != std::string_view::npos);
        REQUIRE(vary.find("Accept-Encoding") != std::string_view::npos);
    }

    SECTION("Convert strong ETag to weak ETag") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("ETag", "\"abc123\"");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        auto etag = res.get_header("ETag");
        REQUIRE(etag.starts_with("W/"));
    }

    SECTION("Keep weak ETag as weak") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.add_header("ETag", "W/\"abc123\"");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        auto etag = res.get_header("ETag");
        REQUIRE(etag == "W/\"abc123\"");
    }
}

TEST_CASE("CompressionMiddleware - Disabled state", "[compression][middleware]") {
    auto config = create_test_config();
    config.enabled = false;
    CompressionMiddleware middleware(config);

    SECTION("Skip compression when disabled") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        size_t original_size = res.body.size();

        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
        REQUIRE(res.get_header("Content-Encoding").empty());
    }
}

TEST_CASE("CompressionMiddleware - Metadata tracking", "[compression][middleware]") {
    auto config = create_test_config();
    CompressionMiddleware middleware(config);

    SECTION("Store compression metadata") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        REQUIRE(ctx.get_metadata("compression_encoding") == "gzip");
        REQUIRE(!ctx.get_metadata("compression_ratio").empty());
        REQUIRE(!ctx.get_metadata("compression_time_us").empty());
        REQUIRE(!ctx.get_metadata("compression_original_size").empty());
        REQUIRE(!ctx.get_metadata("compression_compressed_size").empty());
    }

    SECTION("Store skip reason when too small") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/test";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(512);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        middleware.process_response(ctx);

        REQUIRE(ctx.get_metadata("compression_skip_reason") == "too_small");
    }
}

TEST_CASE("CompressionMiddleware - Per-route config override",
          "[compression][middleware][per-route]") {
    auto global_config = create_test_config();
    global_config.enabled = false;
    global_config.min_size = 2048;

    CompressionMiddleware middleware(global_config);

    SECTION("Per-route enables compression when global disables it") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = true;
        ctx.route_match.compression_config = route_config;

        size_t original_size = res.body.size();
        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Per-route disables compression when global enables it") {
        global_config.enabled = true;
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = false;
        ctx.route_match.compression_config = route_config;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
        REQUIRE(res.get_header("Content-Encoding").empty());
    }

    SECTION("Per-route overrides min_size threshold") {
        global_config.enabled = true;
        global_config.min_size = 10000;
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(5000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = true;
        route_config.min_size = 1024;
        ctx.route_match.compression_config = route_config;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Per-route overrides algorithm priority") {
        global_config.enabled = true;
        global_config.algorithms = {"gzip", "zstd"};
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "br, gzip, zstd"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = true;
        route_config.algorithms = {"br", "gzip"};
        ctx.route_match.compression_config = route_config;

        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.get_header("Content-Encoding") == "br");
    }

    SECTION("Per-route overrides BREACH sensitive paths") {
        global_config.enabled = true;
        global_config.disable_for_paths = {};
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/auth/login";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = true;
        route_config.disable_for_paths = {"/auth/*"};
        ctx.route_match.compression_config = route_config;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() == original_size);
        REQUIRE(ctx.get_metadata("compression_skip_reason") == "breach_sensitive_path");
    }

    SECTION("Per-route overrides content-type filtering") {
        global_config.enabled = true;
        global_config.content_types = {"text/html"};
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        CompressionConfig route_config = create_test_config();
        route_config.enabled = true;
        route_config.content_types = {"application/json", "text/html"};
        ctx.route_match.compression_config = route_config;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("No per-route config falls back to global config") {
        global_config.enabled = true;
        CompressionMiddleware middleware2(global_config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(10240);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }
}

TEST_CASE("CompressionMiddleware - Streaming compression", "[compression][middleware][streaming]") {
    auto config = create_test_config();
    config.enabled = true;
    config.streaming_threshold = 50000;

    CompressionMiddleware middleware(config);

    SECTION("Large response triggers streaming compression with gzip") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/large";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(100000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Large response triggers streaming compression with zstd") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/large";
        req.headers.push_back({"Accept-Encoding", "zstd"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(100000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "zstd");
    }

    SECTION("Large response triggers streaming compression with brotli") {
        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/large";
        req.headers.push_back({"Accept-Encoding", "br"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(100000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "br");
    }

    SECTION("Response below threshold uses buffered compression") {
        config.streaming_threshold = 200000;
        CompressionMiddleware middleware2(config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/medium";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = create_test_data(100000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
    }

    SECTION("Very large response (500KB) with streaming") {
        config.streaming_threshold = 50000;
        CompressionMiddleware middleware2(config);

        Request req;
        Response res;
        req.method = Method::GET;
        req.path = "/very-large";
        req.headers.push_back({"Accept-Encoding", "gzip"});
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/plain");
        res.body_storage = create_test_data(500000);
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;

        size_t original_size = res.body.size();
        auto result = middleware2.process_response(ctx);

        REQUIRE(result == MiddlewareResult::Continue);
        REQUIRE(res.body.size() < original_size);
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
        REQUIRE(res.body.size() < original_size / 2);
    }

    SECTION("Streaming and buffered produce equivalent output") {
        auto streaming_config = create_test_config();
        streaming_config.enabled = true;
        streaming_config.streaming_threshold = 50000;

        auto buffered_config = create_test_config();
        buffered_config.enabled = true;
        buffered_config.streaming_threshold = 200000;

        CompressionMiddleware streaming_mw(streaming_config);
        CompressionMiddleware buffered_mw(buffered_config);

        auto test_data = create_test_data(100000);

        Request req1;
        Response res1;
        req1.method = Method::GET;
        req1.path = "/test";
        req1.headers.push_back({"Accept-Encoding", "gzip"});
        res1.status = StatusCode::OK;
        res1.add_header("Content-Type", "application/json");
        res1.body_storage = test_data;
        res1.body = res1.body_storage;

        ResponseContext ctx1;
        ctx1.request = &req1;
        ctx1.response = &res1;

        Request req2;
        Response res2;
        req2.method = Method::GET;
        req2.path = "/test";
        req2.headers.push_back({"Accept-Encoding", "gzip"});
        res2.status = StatusCode::OK;
        res2.add_header("Content-Type", "application/json");
        res2.body_storage = test_data;
        res2.body = res2.body_storage;

        ResponseContext ctx2;
        ctx2.request = &req2;
        ctx2.response = &res2;

        streaming_mw.process_response(ctx1);
        buffered_mw.process_response(ctx2);

        REQUIRE(res1.body.size() == res2.body.size());
        REQUIRE(std::equal(res1.body.begin(), res1.body.end(), res2.body.begin()));
    }
}

// ============================================================================
// Pre-compressed File Handler Tests
// ============================================================================

TEST_CASE("CompressionMiddleware - Pre-compressed file metadata check",
          "[compression][middleware][precompressed][debug]") {
    Request req;
    req.method = Method::GET;
    req.path = "/test.js";

    Response res;
    res.status = StatusCode::OK;

    ResponseContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.set_metadata("static_file_path", "/tmp/test.js");

    // Verify metadata is set
    REQUIRE(ctx.get_metadata("static_file_path") == "/tmp/test.js");
    REQUIRE(!ctx.get_metadata("static_file_path").empty());
}

TEST_CASE("CompressionMiddleware - Pre-compressed file serving",
          "[compression][middleware][precompressed]") {
    namespace fs = std::filesystem;

    // Create temporary test directory
    fs::path test_dir = fs::temp_directory_path() / "titan_precompressed_test";
    fs::create_directories(test_dir);

    // Helper: Create test files
    auto create_test_files = [&](const std::string& filename,
                                 const std::vector<uint8_t>& original_data) {
        fs::path original_path = test_dir / filename;
        fs::path gzip_path = test_dir / (filename + ".gz");
        fs::path zstd_path = test_dir / (filename + ".zst");
        fs::path brotli_path = test_dir / (filename + ".br");

        // Write original file
        std::ofstream original_file(original_path, std::ios::binary);
        original_file.write(reinterpret_cast<const char*>(original_data.data()),
                            original_data.size());
        original_file.close();

        // Compress and write gzip version
        GzipContext gzip(6);
        auto gzip_data = gzip.compress(original_data.data(), original_data.size());
        std::ofstream gzip_file(gzip_path, std::ios::binary);
        gzip_file.write(reinterpret_cast<const char*>(gzip_data.data()), gzip_data.size());
        gzip_file.close();

        // Compress and write zstd version
        ZstdContext zstd(5);
        auto zstd_data = zstd.compress(original_data.data(), original_data.size());
        std::ofstream zstd_file(zstd_path, std::ios::binary);
        zstd_file.write(reinterpret_cast<const char*>(zstd_data.data()), zstd_data.size());
        zstd_file.close();

        // Compress and write brotli version
        BrotliContext brotli(4);
        auto brotli_data = brotli.compress(original_data.data(), original_data.size());
        std::ofstream brotli_file(brotli_path, std::ios::binary);
        brotli_file.write(reinterpret_cast<const char*>(brotli_data.data()), brotli_data.size());
        brotli_file.close();

        return original_path.string();
    };

    CompressionConfig config = create_test_config();
    config.precompressed.enabled = true;
    config.min_size = 0;  // Disable min size check for these tests
    config.content_types = {"text/plain", "text/html", "application/json", "application/javascript",
                            "text/css"};
    CompressionMiddleware middleware(config);

    SECTION("Serves gzip precompressed file successfully") {
        // Create data larger than default min_size to trigger compression
        std::string test_str(2000, 'a');  // 2KB of 'a' characters
        auto original_data = string_to_bytes(test_str);
        std::string file_path = create_test_files("test.js", original_data);

        Request req;
        req.method = Method::GET;
        req.path = "/test.js";
        req.headers.push_back({"Accept-Encoding", "gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/javascript");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path);

        uint64_t initial_precompressed_hits = compression_metrics.precompressed_hits;

        middleware.process_response(ctx);

        REQUIRE(res.get_header("Content-Encoding") == "gzip");
        REQUIRE(res.get_header("Vary") == "Accept-Encoding");
        REQUIRE(res.body.size() < original_data.size());  // Compressed is smaller
        REQUIRE(compression_metrics.precompressed_hits == initial_precompressed_hits + 1);
    }

    SECTION("Serves zstd precompressed file successfully") {
        std::string test_str(2000, 'b');  // 2KB of 'b' characters
        auto original_data = string_to_bytes(test_str);
        std::string file_path = create_test_files("test.json", original_data);

        Request req;
        req.method = Method::GET;
        req.path = "/test.json";
        req.headers.push_back({"Accept-Encoding", "zstd, gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path);

        middleware.process_response(ctx);

        REQUIRE(res.get_header("Content-Encoding") == "zstd");
        REQUIRE(res.body.size() < original_data.size());
    }

    SECTION("Serves brotli precompressed file successfully") {
        std::string test_str(2000, 'c');  // 2KB of 'c' characters
        auto original_data = string_to_bytes(test_str);
        std::string file_path = create_test_files("test.css", original_data);

        Request req;
        req.method = Method::GET;
        req.path = "/test.css";
        req.headers.push_back({"Accept-Encoding", "br"});  // Only request brotli

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/css");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path);

        middleware.process_response(ctx);

        REQUIRE(res.get_header("Content-Encoding") == "br");
        REQUIRE(res.body.size() < original_data.size());
    }

    SECTION("Falls back to runtime compression when precompressed file doesn't exist") {
        std::string test_str(2000, 'd');  // 2KB of 'd' characters
        auto original_data = string_to_bytes(test_str);
        fs::path file_path = test_dir / "noprecompressed.html";

        // Create only original file, no precompressed versions
        std::ofstream original_file(file_path, std::ios::binary);
        original_file.write(reinterpret_cast<const char*>(original_data.data()),
                            original_data.size());
        original_file.close();

        Request req;
        req.method = Method::GET;
        req.path = "/noprecompressed.html";
        req.headers.push_back({"Accept-Encoding", "gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "text/html");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path.string());

        uint64_t initial_precompressed_hits = compression_metrics.precompressed_hits;

        middleware.process_response(ctx);

        // Should still compress, but using runtime compression
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
        REQUIRE(res.body.size() < original_data.size());
        REQUIRE(compression_metrics.precompressed_hits ==
                initial_precompressed_hits);  // No precompressed hit
    }

    SECTION("Rejects stale precompressed file (older than original)") {
        std::string test_str(2000, 'e');  // 2KB
        auto original_data = string_to_bytes(test_str);
        std::string file_path = create_test_files("stale.js", original_data);

        // Sleep to ensure time passes
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Update original file to be newer
        std::string new_str(2500, 'f');  // 2.5KB
        auto new_data = string_to_bytes(new_str);
        std::ofstream original_file(file_path, std::ios::binary);
        original_file.write(reinterpret_cast<const char*>(new_data.data()), new_data.size());
        original_file.close();

        Request req;
        req.method = Method::GET;
        req.path = "/stale.js";
        req.headers.push_back({"Accept-Encoding", "gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/javascript");
        res.body_storage = new_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path);

        uint64_t initial_precompressed_hits = compression_metrics.precompressed_hits;

        middleware.process_response(ctx);

        // Should fall back to runtime compression (stale precompressed file)
        REQUIRE(compression_metrics.precompressed_hits == initial_precompressed_hits);
    }

    SECTION("Skips precompressed when static_file_path metadata is missing") {
        std::string test_str(2000, 'g');  // 2KB
        auto original_data = string_to_bytes(test_str);

        Request req;
        req.method = Method::GET;
        req.path = "/api/data";
        req.headers.push_back({"Accept-Encoding", "gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/json");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        // Note: no static_file_path metadata

        uint64_t initial_precompressed_hits = compression_metrics.precompressed_hits;

        middleware.process_response(ctx);

        // Should use runtime compression
        REQUIRE(res.get_header("Content-Encoding") == "gzip");
        REQUIRE(compression_metrics.precompressed_hits == initial_precompressed_hits);
    }

    SECTION("Tracks metrics correctly for precompressed files") {
        auto original_data = create_test_data(50000);  // 50KB
        std::string file_path = create_test_files("metrics.js", original_data);

        Request req;
        req.method = Method::GET;
        req.path = "/metrics.js";
        req.headers.push_back({"Accept-Encoding", "gzip"});

        Response res;
        res.status = StatusCode::OK;
        res.add_header("Content-Type", "application/javascript");
        res.body_storage = original_data;
        res.body = res.body_storage;

        ResponseContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.set_metadata("static_file_path", file_path);

        uint64_t initial_bytes_in = compression_metrics.bytes_in;
        uint64_t initial_bytes_out = compression_metrics.bytes_out;
        uint64_t initial_gzip_count = compression_metrics.gzip_count;
        uint64_t initial_requests_compressed = compression_metrics.requests_compressed;

        middleware.process_response(ctx);

        REQUIRE(compression_metrics.bytes_in == initial_bytes_in + original_data.size());
        REQUIRE(compression_metrics.bytes_out > initial_bytes_out);  // Compressed data added
        REQUIRE(compression_metrics.gzip_count == initial_gzip_count + 1);
        REQUIRE(compression_metrics.requests_compressed == initial_requests_compressed + 1);
    }

    // Cleanup: Remove test directory
    fs::remove_all(test_dir);
}
