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

// Titan JWKS Fetcher - Header
// Fetch JWT verification keys from JWKS endpoints (Auth0, Keycloak, etc.)

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "jwt.hpp"

namespace titan::core {

/// JWKS fetcher configuration
struct JwksConfig {
    std::string url;                          // JWKS endpoint URL
    uint32_t refresh_interval_seconds = 3600; // Default: 1 hour
    uint32_t timeout_seconds = 10;            // HTTP timeout
    uint32_t retry_max = 3;                   // Max retries before circuit break
    uint32_t circuit_breaker_seconds = 300;   // Cooldown after failures (5 min)
};

/// JWKS fetcher state
enum class JwksFetcherState {
    Healthy,      // Successfully fetching keys
    Degraded,     // Using cached keys, fetch failing
    CircuitOpen   // Circuit breaker open, using static fallback
};

/// JWK (JSON Web Key) parsed from JWKS endpoint
struct JsonWebKey {
    std::string kty;   // Key type: "RSA" or "EC"
    std::string alg;   // Algorithm: "RS256", "ES256"
    std::string kid;   // Key ID
    std::string use;   // Key use: "sig" for signature

    // RSA-specific fields
    std::string n;     // Modulus (base64url)
    std::string e;     // Exponent (base64url)

    // EC-specific fields
    std::string crv;   // Curve: "P-256" for ES256
    std::string x;     // X coordinate (base64url)
    std::string y;     // Y coordinate (base64url)

    [[nodiscard]] static std::optional<JsonWebKey> parse(const std::string& json);
};

/// JWKS fetcher (background thread fetches keys periodically)
class JwksFetcher {
public:
    explicit JwksFetcher(JwksConfig config);
    ~JwksFetcher();

    // Non-copyable, non-movable (owns thread)
    JwksFetcher(const JwksFetcher&) = delete;
    JwksFetcher& operator=(const JwksFetcher&) = delete;
    JwksFetcher(JwksFetcher&&) = delete;
    JwksFetcher& operator=(JwksFetcher&&) = delete;

    /// Start background fetcher thread
    void start();

    /// Stop background fetcher thread
    void stop();

    /// Fetch keys synchronously (blocking, called by background thread)
    [[nodiscard]] bool fetch_keys();

    /// Get current key manager (RCU read, thread-safe)
    [[nodiscard]] std::shared_ptr<KeyManager> get_keys() const;

    /// State monitoring
    [[nodiscard]] JwksFetcherState get_state() const { return state_.load(); }
    [[nodiscard]] uint64_t get_last_success_timestamp() const { return last_success_ts_; }
    [[nodiscard]] uint32_t get_consecutive_failures() const { return consecutive_failures_; }

private:
    /// Background thread loop
    void fetch_loop();

    /// HTTP GET request (returns response body or nullopt on error)
    [[nodiscard]] std::optional<std::string> http_get(const std::string& url);

    /// Parse JWKS JSON (array of JWK objects)
    [[nodiscard]] std::optional<std::vector<JsonWebKey>> parse_jwks(const std::string& json);

    /// Convert JWK to VerificationKey
    [[nodiscard]] std::optional<VerificationKey> jwk_to_verification_key(const JsonWebKey& jwk);

    /// RSA: Convert n/e (base64url) to EVP_PKEY
    [[nodiscard]] EVP_PKEY* rsa_jwk_to_evp_pkey(const JsonWebKey& jwk);

    /// EC: Convert crv/x/y (base64url) to EVP_PKEY
    [[nodiscard]] EVP_PKEY* ec_jwk_to_evp_pkey(const JsonWebKey& jwk);

    /// Calculate exponential backoff delay
    [[nodiscard]] uint32_t calculate_backoff(uint32_t attempt) const;

    JwksConfig config_;

    // RCU pattern: atomic pointer swap for hot-reload
    std::atomic<std::shared_ptr<KeyManager>> keys_;

    // State tracking
    std::atomic<JwksFetcherState> state_{JwksFetcherState::Healthy};
    std::atomic<uint64_t> last_success_ts_{0};
    std::atomic<uint32_t> consecutive_failures_{0};

    // Background thread
    std::unique_ptr<std::thread> fetch_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace titan::core
