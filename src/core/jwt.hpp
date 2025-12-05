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

// Titan JWT Authentication - Header
// RFC 7519 JWT validation with RS256/ES256/HS256 support

#pragma once

#include <cstdint>
#include <ctime>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rsa.h>

namespace titan::core {

// Forward declaration
class JwksFetcher;

/// JWT algorithm types
enum class JwtAlgorithm {
    RS256,  // RSA + SHA-256 (asymmetric)
    ES256,  // ECDSA P-256 + SHA-256 (asymmetric)
    HS256,  // HMAC + SHA-256 (symmetric)
    None    // No signature (rejected for security)
};

/// JWT header (decoded from first part)
struct JwtHeader {
    JwtAlgorithm algorithm;
    std::string type;     // Usually "JWT"
    std::string key_id;   // Optional kid for key rotation

    [[nodiscard]] static std::optional<JwtHeader> parse(std::string_view json);
};

/// JWT claims (decoded from payload)
struct JwtClaims {
    // Standard claims (RFC 7519)
    std::string sub;   // Subject (user ID)
    std::string iss;   // Issuer
    std::string aud;   // Audience
    int64_t exp = 0;   // Expiration time (Unix timestamp)
    int64_t iat = 0;   // Issued at
    int64_t nbf = 0;   // Not before
    std::string jti;   // JWT ID (for revocation)

    // Custom claims (application-specific)
    std::string scope;      // e.g., "read:users write:posts"
    nlohmann::json custom;  // Store full claims for advanced use

    [[nodiscard]] static std::optional<JwtClaims> parse(std::string_view json);
};

/// Cryptographic key for signature verification
struct VerificationKey {
    JwtAlgorithm algorithm;
    std::string key_id;

    // Key material (only one is set based on algorithm)
    EVP_PKEY* public_key = nullptr;     // For RS256/ES256 (OpenSSL key)
    std::vector<uint8_t> hmac_secret;   // For HS256

    ~VerificationKey();

    // Non-copyable (owns OpenSSL resources)
    VerificationKey(const VerificationKey&) = delete;
    VerificationKey& operator=(const VerificationKey&) = delete;
    VerificationKey(VerificationKey&&) noexcept;
    VerificationKey& operator=(VerificationKey&&) noexcept;

    VerificationKey() = default;

    /// Load RSA/ECDSA public key from PEM file
    [[nodiscard]] static std::optional<VerificationKey> load_public_key(JwtAlgorithm alg,
                                                                         std::string_view key_id,
                                                                         std::string_view pem_path);

    /// Load HMAC secret from base64-encoded string
    [[nodiscard]] static std::optional<VerificationKey> load_hmac_secret(std::string_view key_id,
                                                                          std::string_view secret);
};

/// Key manager (supports multiple keys for rotation)
class KeyManager {
public:
    KeyManager() = default;
    ~KeyManager() = default;

    // Non-copyable, movable
    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;
    KeyManager(KeyManager&&) noexcept = default;
    KeyManager& operator=(KeyManager&&) noexcept = default;

    /// Add verification key
    void add_key(VerificationKey key);

    /// Get key by algorithm and key ID
    [[nodiscard]] const VerificationKey* get_key(JwtAlgorithm alg,
                                                  std::string_view key_id) const;

    /// Get key count
    [[nodiscard]] size_t key_count() const noexcept { return keys_.size(); }

    /// Clear all keys
    void clear() { keys_.clear(); }

private:
    std::vector<VerificationKey> keys_;
};

/// JWT validation result
struct ValidationResult {
    bool valid = false;
    JwtClaims claims;
    std::string error;

    [[nodiscard]] static ValidationResult success(JwtClaims claims) {
        return {true, std::move(claims), ""};
    }

    [[nodiscard]] static ValidationResult failure(std::string error) {
        return {false, {}, std::move(error)};
    }

    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

/// Thread-local JWT token cache (LRU eviction)
class ThreadLocalTokenCache {
public:
    explicit ThreadLocalTokenCache(size_t capacity);
    ~ThreadLocalTokenCache() = default;

    // Non-copyable, movable
    ThreadLocalTokenCache(const ThreadLocalTokenCache&) = delete;
    ThreadLocalTokenCache& operator=(const ThreadLocalTokenCache&) = delete;
    ThreadLocalTokenCache(ThreadLocalTokenCache&&) noexcept = default;
    ThreadLocalTokenCache& operator=(ThreadLocalTokenCache&&) noexcept = default;

    /// Cached token entry
    struct CachedToken {
        JwtClaims claims;
        std::time_t cached_at;
    };

    /// Get cached token (returns nullopt on miss or expired)
    [[nodiscard]] std::optional<CachedToken> get(std::string_view token);

    /// Put token in cache
    void put(std::string_view token, JwtClaims claims);

    /// Clear cache
    void clear();

    /// Get cache statistics
    [[nodiscard]] size_t size() const noexcept { return cache_.size(); }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

private:
    size_t capacity_;
    std::list<std::pair<std::string, CachedToken>> lru_list_;
    std::unordered_map<std::string, decltype(lru_list_)::iterator> cache_;
};

/// JWT validator configuration
struct JwtValidatorConfig {
    // Claims validation
    bool require_exp = true;
    bool require_sub = false;
    std::vector<std::string> allowed_issuers;
    std::vector<std::string> allowed_audiences;
    int64_t clock_skew_seconds = 60;  // Tolerance for time-based claims

    // Caching
    bool cache_enabled = true;
    size_t cache_capacity = 10000;  // Tokens per thread
};

/// JWT validator (stateless validation logic)
class JwtValidator {
public:
    explicit JwtValidator(JwtValidatorConfig config);
    ~JwtValidator() = default;

    // Non-copyable, movable
    JwtValidator(const JwtValidator&) = delete;
    JwtValidator& operator=(const JwtValidator&) = delete;
    JwtValidator(JwtValidator&&) noexcept = default;
    JwtValidator& operator=(JwtValidator&&) noexcept = default;

    /// Set static key manager (always available, fallback when JWKS fails)
    void set_key_manager(std::shared_ptr<KeyManager> keys) { static_keys_ = std::move(keys); }

    /// Set JWKS fetcher for dynamic key loading (optional)
    void set_jwks_fetcher(std::shared_ptr<JwksFetcher> fetcher) { jwks_fetcher_ = std::move(fetcher); }

    /// Validate JWT token (with caching)
    [[nodiscard]] ValidationResult validate(std::string_view token);

    /// Get cache statistics
    [[nodiscard]] size_t cache_size() const noexcept { return cache_ ? cache_->size() : 0; }

private:
    /// Validate token without cache (full validation)
    [[nodiscard]] ValidationResult validate_uncached(std::string_view token);

    /// Verify signature
    [[nodiscard]] bool verify_signature(JwtAlgorithm alg, std::string_view message,
                                        std::string_view signature, const VerificationKey* key);

    /// Validate claims (exp, nbf, iss, aud)
    [[nodiscard]] ValidationResult validate_claims(const JwtClaims& claims);

    /// Get merged key manager (JWKS keys + static keys)
    [[nodiscard]] std::shared_ptr<KeyManager> get_merged_keys();

    JwtValidatorConfig config_;
    std::shared_ptr<KeyManager> static_keys_;      // Static keys from config
    std::shared_ptr<JwksFetcher> jwks_fetcher_;    // Dynamic JWKS fetcher (optional)
    std::unique_ptr<ThreadLocalTokenCache> cache_;
};

// Utility functions

/// Base64url encode (RFC 4648)
[[nodiscard]] std::string base64url_encode(std::string_view input);

/// Base64url decode (RFC 4648)
[[nodiscard]] std::optional<std::string> base64url_decode(std::string_view input);

/// Parse algorithm string to enum
[[nodiscard]] std::optional<JwtAlgorithm> parse_algorithm(std::string_view alg_str);

/// Convert algorithm enum to string
[[nodiscard]] std::string_view algorithm_to_string(JwtAlgorithm alg);

}  // namespace titan::core
