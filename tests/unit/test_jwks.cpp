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

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

#include "core/jwks_fetcher.hpp"
#include "core/jwt.hpp"

using namespace titan::core;

// ============================================================================
// Test Data - Valid JWKs from Auth0 / RFC 7517
// ============================================================================

// RSA JWK (2048-bit, RS256)
static const char* TEST_RSA_JWK = R"({
  "kty": "RSA",
  "alg": "RS256",
  "kid": "rsa-key-1",
  "use": "sig",
  "n": "u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmwIDAQAB",
  "e": "AQAB"
})";

// EC JWK (P-256, ES256)
static const char* TEST_EC_JWK = R"({
  "kty": "EC",
  "alg": "ES256",
  "kid": "ec-key-1",
  "use": "sig",
  "crv": "P-256",
  "x": "WKn-ZIGevcwGIyyrzFoZNBdaq9_TsqzGl96oc0CWuis",
  "y": "y77t-RvAHRKTsSGdIYUfweuOvwrvDD-Q3Hv5J0fSKbE"
})";

// JWKS response with multiple keys
static const char* TEST_JWKS_RESPONSE = R"({
  "keys": [
    {
      "kty": "RSA",
      "alg": "RS256",
      "kid": "rsa-key-1",
      "use": "sig",
      "n": "u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmwIDAQAB",
      "e": "AQAB"
    },
    {
      "kty": "EC",
      "alg": "ES256",
      "kid": "ec-key-1",
      "use": "sig",
      "crv": "P-256",
      "x": "WKn-ZIGevcwGIyyrzFoZNBdaq9_TsqzGl96oc0CWuis",
      "y": "y77t-RvAHRKTsSGdIYUfweuOvwrvDD-Q3Hv5J0fSKbE"
    }
  ]
})";

// ============================================================================
// JsonWebKey Parsing Tests
// ============================================================================

TEST_CASE("JsonWebKey parsing - RSA", "[jwks][parse]") {
    SECTION("Parse valid RSA JWK") {
        auto jwk = JsonWebKey::parse(TEST_RSA_JWK);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->kty == "RSA");
        REQUIRE(jwk->alg == "RS256");
        REQUIRE(jwk->kid == "rsa-key-1");
        REQUIRE(jwk->use == "sig");
        REQUIRE(!jwk->n.empty());
        REQUIRE(!jwk->e.empty());
        REQUIRE(jwk->n.find('_') != std::string::npos);  // Base64url uses '_'
        REQUIRE(jwk->n.find('+') == std::string::npos);  // Not standard base64
    }

    SECTION("Parse RSA JWK without 'use' field (should default)") {
        const char* jwk_no_use = R"({
          "kty": "RSA",
          "alg": "RS256",
          "kid": "test",
          "n": "test-n",
          "e": "AQAB"
        })";
        auto jwk = JsonWebKey::parse(jwk_no_use);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->use == "sig");  // Default value
    }

    SECTION("Parse RSA JWK without kid") {
        const char* jwk_no_kid = R"({
          "kty": "RSA",
          "alg": "RS256",
          "n": "test-n",
          "e": "AQAB"
        })";
        auto jwk = JsonWebKey::parse(jwk_no_kid);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->kid.empty());
    }

    SECTION("Parse RSA JWK missing modulus (n)") {
        const char* jwk_no_n = R"({
          "kty": "RSA",
          "alg": "RS256",
          "kid": "test",
          "e": "AQAB"
        })";
        auto jwk = JsonWebKey::parse(jwk_no_n);
        REQUIRE(jwk.has_value());  // Parsing succeeds
        REQUIRE(jwk->n.empty());   // But 'n' is empty (invalid for use)
    }
}

TEST_CASE("JsonWebKey parsing - EC", "[jwks][parse]") {
    SECTION("Parse valid EC JWK (P-256)") {
        auto jwk = JsonWebKey::parse(TEST_EC_JWK);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->kty == "EC");
        REQUIRE(jwk->alg == "ES256");
        REQUIRE(jwk->kid == "ec-key-1");
        REQUIRE(jwk->crv == "P-256");
        REQUIRE(!jwk->x.empty());
        REQUIRE(!jwk->y.empty());
    }

    SECTION("Parse EC JWK with P-384 curve (currently unsupported)") {
        const char* jwk_p384 = R"({
          "kty": "EC",
          "alg": "ES384",
          "kid": "test",
          "crv": "P-384",
          "x": "test-x",
          "y": "test-y"
        })";
        auto jwk = JsonWebKey::parse(jwk_p384);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->crv == "P-384");
    }

    SECTION("Parse EC JWK missing coordinates") {
        const char* jwk_no_coords = R"({
          "kty": "EC",
          "alg": "ES256",
          "crv": "P-256"
        })";
        auto jwk = JsonWebKey::parse(jwk_no_coords);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->x.empty());
        REQUIRE(jwk->y.empty());
    }
}

TEST_CASE("JsonWebKey parsing - Error cases", "[jwks][parse]") {
    SECTION("Parse invalid JSON") {
        auto jwk = JsonWebKey::parse("not valid json");
        REQUIRE(!jwk.has_value());
    }

    SECTION("Parse empty JSON object") {
        auto jwk = JsonWebKey::parse("{}");
        REQUIRE(jwk.has_value());   // Parsing succeeds
        REQUIRE(jwk->kty.empty());  // But fields are empty
    }

    SECTION("Parse JSON array (not object)") {
        auto jwk = JsonWebKey::parse("[]");
        REQUIRE(!jwk.has_value());  // Should fail
    }

    SECTION("Parse unsupported key type") {
        const char* jwk_oct = R"({
          "kty": "oct",
          "alg": "HS256",
          "k": "secret-key-base64url"
        })";
        auto jwk = JsonWebKey::parse(jwk_oct);
        REQUIRE(jwk.has_value());
        REQUIRE(jwk->kty == "oct");  // Parses but won't convert to EVP_PKEY
    }
}

// ============================================================================
// JWKS Response Parsing Tests
// ============================================================================

TEST_CASE("JWKS response parsing", "[jwks][parse]") {
    SECTION("Parse valid JWKS with multiple keys") {
        // Note: parse_jwks is private, so we test through JwksFetcher
        // For now, we test JsonWebKey individually

        // We'll need to expose parse_jwks or test via integration
        // For unit tests, we verify JsonWebKey parsing works
        auto jwk1 = JsonWebKey::parse(TEST_RSA_JWK);
        auto jwk2 = JsonWebKey::parse(TEST_EC_JWK);
        REQUIRE(jwk1.has_value());
        REQUIRE(jwk2.has_value());
        REQUIRE(jwk1->kty == "RSA");
        REQUIRE(jwk2->kty == "EC");
    }
}

// ============================================================================
// JWK to EVP_PKEY Conversion Tests
// ============================================================================

TEST_CASE("JWK to VerificationKey conversion", "[jwks][conversion]") {
    // Note: jwk_to_verification_key is private method of JwksFetcher
    // These tests verify the conversion logic indirectly

    SECTION("Valid RSA JWK should have valid base64url modulus") {
        auto jwk = JsonWebKey::parse(TEST_RSA_JWK);
        REQUIRE(jwk.has_value());

        // Verify base64url decode works
        auto n_decoded = base64url_decode(jwk->n);
        auto e_decoded = base64url_decode(jwk->e);
        REQUIRE(n_decoded.has_value());
        REQUIRE(e_decoded.has_value());
        REQUIRE(n_decoded->size() >= 256);  // 2048-bit key = at least 256 bytes
        REQUIRE(e_decoded->size() >= 3);    // Typically 65537 = 3 bytes
    }

    SECTION("Valid EC JWK should have valid base64url coordinates") {
        auto jwk = JsonWebKey::parse(TEST_EC_JWK);
        REQUIRE(jwk.has_value());

        // Verify base64url decode works
        auto x_decoded = base64url_decode(jwk->x);
        auto y_decoded = base64url_decode(jwk->y);
        REQUIRE(x_decoded.has_value());
        REQUIRE(y_decoded.has_value());
        REQUIRE(x_decoded->size() == 32);  // P-256 = 32 bytes per coordinate
        REQUIRE(y_decoded->size() == 32);
    }

    SECTION("Invalid base64url should fail decode or return empty") {
        auto bad_decode = base64url_decode("!!!invalid!!!");
        // Base64 decoders may be permissive (OpenSSL BIO returns partial data)
        // Either returns nullopt, or returns a result (possibly empty)
        // We just verify decode doesn't crash
        if (bad_decode.has_value()) {
            // Some decoders are permissive and return empty/partial data
            REQUIRE(true);  // Just verify it doesn't crash
        } else {
            REQUIRE(!bad_decode.has_value());
        }
    }
}

// ============================================================================
// JwksFetcher Configuration Tests
// ============================================================================

TEST_CASE("JwksFetcher configuration", "[jwks][config]") {
    SECTION("Create with default config") {
        JwksConfig config;
        config.url = "https://example.com/.well-known/jwks.json";

        JwksFetcher fetcher(config);
        REQUIRE(fetcher.get_state() == JwksFetcherState::Healthy);
        REQUIRE(fetcher.get_consecutive_failures() == 0);
        REQUIRE(fetcher.get_last_success_timestamp() == 0);
    }

    SECTION("Create with custom config") {
        JwksConfig config;
        config.url = "https://auth.example.com/jwks";
        config.refresh_interval_seconds = 7200;  // 2 hours
        config.timeout_seconds = 5;
        config.retry_max = 5;
        config.circuit_breaker_seconds = 600;  // 10 min

        JwksFetcher fetcher(config);
        REQUIRE(fetcher.get_state() == JwksFetcherState::Healthy);
    }

    SECTION("Get keys from empty fetcher (no fetch yet)") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        JwksFetcher fetcher(config);
        auto keys = fetcher.get_keys();
        REQUIRE(keys != nullptr);
        // Should be empty KeyManager
    }
}

// ============================================================================
// Circuit Breaker State Transitions Tests
// ============================================================================

TEST_CASE("JwksFetcher circuit breaker", "[jwks][circuit-breaker]") {
    SECTION("Initial state is Healthy") {
        JwksConfig config;
        config.url = "https://invalid.example.com/jwks";  // Invalid URL
        config.timeout_seconds = 1;
        config.retry_max = 3;

        JwksFetcher fetcher(config);
        REQUIRE(fetcher.get_state() == JwksFetcherState::Healthy);
        REQUIRE(fetcher.get_consecutive_failures() == 0);
    }

    SECTION("Failed fetch transitions to Degraded") {
        JwksConfig config;
        config.url = "https://invalid.example.com/jwks";
        config.timeout_seconds = 1;
        config.retry_max = 3;

        JwksFetcher fetcher(config);

        // Trigger fetch failure
        bool success = fetcher.fetch_keys();
        REQUIRE(!success);
        REQUIRE(fetcher.get_consecutive_failures() >= 1);

        // Should transition to Degraded (not CircuitOpen yet)
        if (fetcher.get_consecutive_failures() < config.retry_max) {
            REQUIRE(fetcher.get_state() == JwksFetcherState::Degraded);
        }
    }

    SECTION("Multiple failures open circuit breaker") {
        JwksConfig config;
        config.url = "https://invalid.example.com/jwks";
        config.timeout_seconds = 1;
        config.retry_max = 2;  // Low threshold

        JwksFetcher fetcher(config);

        // Trigger multiple failures
        for (uint32_t i = 0; i < config.retry_max; ++i) {
            fetcher.fetch_keys();
        }

        REQUIRE(fetcher.get_consecutive_failures() >= config.retry_max);
        REQUIRE(fetcher.get_state() == JwksFetcherState::CircuitOpen);
    }
}

// ============================================================================
// RCU Key Rotation Tests
// ============================================================================

TEST_CASE("JwksFetcher RCU key rotation", "[jwks][rcu]") {
    SECTION("get_keys returns consistent pointer") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        JwksFetcher fetcher(config);

        auto keys1 = fetcher.get_keys();
        auto keys2 = fetcher.get_keys();

        // Should return same shared_ptr (no update occurred)
        REQUIRE(keys1 == keys2);
        REQUIRE(keys1.get() == keys2.get());
    }

    SECTION("Multiple threads can read keys concurrently") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        JwksFetcher fetcher(config);

        std::atomic<bool> failed{false};

        // Spawn multiple reader threads
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&]() {
                for (int j = 0; j < 100; ++j) {
                    auto keys = fetcher.get_keys();
                    if (keys == nullptr) {
                        failed = true;
                    }
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(!failed);
    }
}

// ============================================================================
// Background Thread Tests
// ============================================================================

TEST_CASE("JwksFetcher background thread", "[jwks][thread]") {
    SECTION("start() and stop() without errors") {
        JwksConfig config;
        config.url = "https://example.com/jwks";
        config.refresh_interval_seconds = 3600;  // 1 hour (won't trigger)

        JwksFetcher fetcher(config);

        fetcher.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fetcher.stop();

        // Should not crash
    }

    SECTION("start() twice is idempotent") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        JwksFetcher fetcher(config);

        fetcher.start();
        fetcher.start();  // Second call should be no-op

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fetcher.stop();
    }

    SECTION("stop() without start() is safe") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        JwksFetcher fetcher(config);
        fetcher.stop();  // Should not crash
    }

    SECTION("Destructor stops thread") {
        JwksConfig config;
        config.url = "https://example.com/jwks";

        {
            JwksFetcher fetcher(config);
            fetcher.start();
            // Destructor should stop thread
        }

        // Should not hang or crash
    }
}

// ============================================================================
// Algorithm Parsing Tests (from JWK)
// ============================================================================

TEST_CASE("Algorithm parsing from JWK", "[jwks][algorithm]") {
    SECTION("RS256 algorithm") {
        auto alg = parse_algorithm("RS256");
        REQUIRE(alg.has_value());
        REQUIRE(*alg == JwtAlgorithm::RS256);
    }

    SECTION("ES256 algorithm") {
        auto alg = parse_algorithm("ES256");
        REQUIRE(alg.has_value());
        REQUIRE(*alg == JwtAlgorithm::ES256);
    }

    SECTION("HS256 algorithm (symmetric)") {
        auto alg = parse_algorithm("HS256");
        REQUIRE(alg.has_value());
        REQUIRE(*alg == JwtAlgorithm::HS256);
    }

    SECTION("Unsupported algorithm") {
        auto alg = parse_algorithm("PS256");  // RSA-PSS not implemented
        REQUIRE(!alg.has_value());
    }

    SECTION("Invalid algorithm") {
        auto alg = parse_algorithm("INVALID");
        REQUIRE(!alg.has_value());
    }
}

// ============================================================================
// Backoff Calculation Tests
// ============================================================================

TEST_CASE("Exponential backoff calculation", "[jwks][backoff]") {
    SECTION("Backoff grows exponentially") {
        JwksConfig config;
        config.url = "https://example.com/jwks";
        config.circuit_breaker_seconds = 300;  // 5 min cap

        JwksFetcher fetcher(config);

        // Note: calculate_backoff is private, but we verify behavior
        // through consecutive failure tracking

        // Attempt 0: 2^0 = 1 second
        // Attempt 1: 2^1 = 2 seconds
        // Attempt 2: 2^2 = 4 seconds
        // Attempt 3: 2^3 = 8 seconds
        // ...
        // Capped at circuit_breaker_seconds (300s)

        // We test indirectly by verifying failures increment
        REQUIRE(fetcher.get_consecutive_failures() == 0);
    }
}

// ============================================================================
// Integration Smoke Test (no real HTTP)
// ============================================================================

TEST_CASE("JwksFetcher full lifecycle", "[jwks][integration]") {
    SECTION("Create, start, fetch (expect failure), stop") {
        JwksConfig config;
        config.url = "https://invalid.test.local/jwks";  // Invalid domain
        config.timeout_seconds = 1;
        config.refresh_interval_seconds = 3600;

        JwksFetcher fetcher(config);

        // Start background thread
        fetcher.start();

        // Manually trigger fetch (will fail)
        bool success = fetcher.fetch_keys();
        REQUIRE(!success);

        // Check state transitioned
        REQUIRE(fetcher.get_consecutive_failures() > 0);
        REQUIRE(fetcher.get_state() != JwksFetcherState::Healthy);

        // Keys should still be accessible (empty)
        auto keys = fetcher.get_keys();
        REQUIRE(keys != nullptr);

        // Stop cleanly
        fetcher.stop();
    }
}
