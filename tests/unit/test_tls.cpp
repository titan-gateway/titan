// Titan TLS Tests
// Unit tests for TLS/SSL utilities and ALPN negotiation

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "../../src/core/tls.hpp"

using namespace titan::core;

// ============================
// TLS Context Creation
// ============================

TEST_CASE("TLS context creation with invalid certificates", "[tls][context]") {
    SECTION("Fail with non-existent certificate file") {
        std::error_code error;
        auto ctx = TlsContext::create("/nonexistent/cert.pem", "/workspace/certs/key.pem",
                                      std::vector<std::string>{"h2"}, error);

        REQUIRE_FALSE(ctx.has_value());
        REQUIRE(error);
    }

    SECTION("Fail with non-existent key file") {
        std::error_code error;
        auto ctx = TlsContext::create("/workspace/certs/cert.pem", "/nonexistent/key.pem",
                                      std::vector<std::string>{"h2"}, error);

        REQUIRE_FALSE(ctx.has_value());
        REQUIRE(error);
    }
}

// ============================
// TLS Handshake States
// ============================

TEST_CASE("TLS handshake result enum", "[tls][handshake]") {
    SECTION("Handshake result values are distinct") {
        REQUIRE(TlsHandshakeResult::Complete != TlsHandshakeResult::WantRead);
        REQUIRE(TlsHandshakeResult::Complete != TlsHandshakeResult::WantWrite);
        REQUIRE(TlsHandshakeResult::Complete != TlsHandshakeResult::Error);
        REQUIRE(TlsHandshakeResult::WantRead != TlsHandshakeResult::WantWrite);
    }
}

// ============================
// ALPN Protocol Negotiation
// ============================

TEST_CASE("ALPN protocol selection", "[tls][alpn]") {
    SECTION("get_alpn_protocol returns empty for nullptr") {
        auto protocol = get_alpn_protocol(nullptr);
        REQUIRE(protocol.empty());
    }
}

// ============================
// OpenSSL Initialization
// ============================

TEST_CASE("OpenSSL initialization", "[tls][init]") {
    SECTION("Initialize and cleanup OpenSSL") {
        // These should not crash
        initialize_openssl();
        cleanup_openssl();

        // Re-initialize after cleanup (should be safe)
        initialize_openssl();
    }
}

// ============================
// Error Handling
// ============================

TEST_CASE("TLS error category", "[tls][error]") {
    SECTION("TLS error category has correct name") {
        const auto& category = tls_category();
        REQUIRE(std::string(category.name()) == "tls");
    }

    SECTION("TLS error code can be created") {
        auto error = make_tls_error();
        // Error code should be valid (may or may not have an actual error)
        REQUIRE(error.category().name() == std::string("tls"));
    }
}
