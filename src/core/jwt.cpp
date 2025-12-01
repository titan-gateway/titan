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

// Titan JWT Authentication - Implementation

#include "jwt.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

namespace titan::core {

// ============================================================================
// Utility Functions: Base64url encoding/decoding
// ============================================================================

std::string base64url_encode(std::string_view input) {
    if (input.empty()) {
        return "";
    }

    // Standard base64 encode
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);  // No newlines

    BIO_write(b64, input.data(), static_cast<int>(input.size()));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);

    // Convert base64 to base64url (RFC 4648)
    // Replace '+' with '-', '/' with '_', remove '='
    std::replace(result.begin(), result.end(), '+', '-');
    std::replace(result.begin(), result.end(), '/', '_');
    result.erase(std::remove(result.begin(), result.end(), '='), result.end());

    return result;
}

std::optional<std::string> base64url_decode(std::string_view input) {
    if (input.empty()) {
        return "";
    }

    // Convert base64url to standard base64
    std::string base64(input);
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');

    // Add padding if needed
    size_t padding = (4 - (base64.size() % 4)) % 4;
    base64.append(padding, '=');

    // Decode base64
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(base64.data(), static_cast<int>(base64.size()));
    bmem = BIO_push(b64, bmem);
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    std::vector<char> buffer(base64.size());
    int decoded_size = BIO_read(bmem, buffer.data(), static_cast<int>(buffer.size()));
    BIO_free_all(bmem);

    if (decoded_size < 0) {
        return std::nullopt;
    }

    return std::string(buffer.data(), decoded_size);
}

// ============================================================================
// Algorithm Utilities
// ============================================================================

std::optional<JwtAlgorithm> parse_algorithm(std::string_view alg_str) {
    if (alg_str == "RS256") {
        return JwtAlgorithm::RS256;
    } else if (alg_str == "ES256") {
        return JwtAlgorithm::ES256;
    } else if (alg_str == "HS256") {
        return JwtAlgorithm::HS256;
    } else if (alg_str == "none") {
        return JwtAlgorithm::None;
    }
    return std::nullopt;
}

std::string_view algorithm_to_string(JwtAlgorithm alg) {
    switch (alg) {
        case JwtAlgorithm::RS256:
            return "RS256";
        case JwtAlgorithm::ES256:
            return "ES256";
        case JwtAlgorithm::HS256:
            return "HS256";
        case JwtAlgorithm::None:
            return "none";
    }
    return "unknown";
}

// ============================================================================
// JwtHeader Implementation
// ============================================================================

std::optional<JwtHeader> JwtHeader::parse(std::string_view json) {
    try {
        auto j = nlohmann::json::parse(json);

        JwtHeader header;

        // Parse algorithm (required)
        if (!j.contains("alg")) {
            return std::nullopt;
        }
        std::string alg_str = j["alg"];
        auto alg = parse_algorithm(alg_str);
        if (!alg) {
            return std::nullopt;
        }
        header.algorithm = *alg;

        // Parse type (optional, defaults to "JWT")
        header.type = j.value("typ", "JWT");

        // Parse key ID (optional)
        header.key_id = j.value("kid", "");

        return header;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================================
// JwtClaims Implementation
// ============================================================================

std::optional<JwtClaims> JwtClaims::parse(std::string_view json) {
    try {
        auto j = nlohmann::json::parse(json);

        JwtClaims claims;

        // Parse standard claims
        claims.sub = j.value("sub", "");
        claims.iss = j.value("iss", "");
        claims.aud = j.value("aud", "");
        claims.exp = j.value("exp", int64_t(0));
        claims.iat = j.value("iat", int64_t(0));
        claims.nbf = j.value("nbf", int64_t(0));
        claims.jti = j.value("jti", "");

        // Parse custom claims
        claims.scope = j.value("scope", "");
        claims.custom = j;  // Store full JSON for advanced use

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================================
// VerificationKey Implementation
// ============================================================================

VerificationKey::~VerificationKey() {
    if (public_key) {
        EVP_PKEY_free(public_key);
        public_key = nullptr;
    }
}

VerificationKey::VerificationKey(VerificationKey&& other) noexcept
    : algorithm(other.algorithm),
      key_id(std::move(other.key_id)),
      public_key(other.public_key),
      hmac_secret(std::move(other.hmac_secret)) {
    other.public_key = nullptr;
}

VerificationKey& VerificationKey::operator=(VerificationKey&& other) noexcept {
    if (this != &other) {
        if (public_key) {
            EVP_PKEY_free(public_key);
        }

        algorithm = other.algorithm;
        key_id = std::move(other.key_id);
        public_key = other.public_key;
        hmac_secret = std::move(other.hmac_secret);

        other.public_key = nullptr;
    }
    return *this;
}

std::optional<VerificationKey> VerificationKey::load_public_key(JwtAlgorithm alg,
                                                                 std::string_view key_id,
                                                                 std::string_view pem_path) {
    // Open PEM file
    FILE* fp = fopen(std::string(pem_path).c_str(), "r");
    if (!fp) {
        return std::nullopt;
    }

    // Read public key
    EVP_PKEY* pkey = PEM_read_PUBKEY(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!pkey) {
        return std::nullopt;
    }

    // Validate key type matches algorithm
    int key_type = EVP_PKEY_base_id(pkey);
    if (alg == JwtAlgorithm::RS256 && key_type != EVP_PKEY_RSA) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }
    if (alg == JwtAlgorithm::ES256 && key_type != EVP_PKEY_EC) {
        EVP_PKEY_free(pkey);
        return std::nullopt;
    }

    VerificationKey key;
    key.algorithm = alg;
    key.key_id = std::string(key_id);
    key.public_key = pkey;

    return key;
}

std::optional<VerificationKey> VerificationKey::load_hmac_secret(std::string_view key_id,
                                                                  std::string_view secret) {
    // Decode base64-encoded secret
    auto decoded = base64url_decode(secret);
    if (!decoded || decoded->empty()) {
        return std::nullopt;
    }

    VerificationKey key;
    key.algorithm = JwtAlgorithm::HS256;
    key.key_id = std::string(key_id);
    key.hmac_secret.assign(decoded->begin(), decoded->end());

    return key;
}

// ============================================================================
// KeyManager Implementation
// ============================================================================

void KeyManager::add_key(VerificationKey key) {
    keys_.push_back(std::move(key));
}

const VerificationKey* KeyManager::get_key(JwtAlgorithm alg, std::string_view key_id) const {
    for (const auto& key : keys_) {
        if (key.algorithm == alg && (key_id.empty() || key.key_id == key_id)) {
            return &key;
        }
    }
    return nullptr;
}

// ============================================================================
// ThreadLocalTokenCache Implementation
// ============================================================================

ThreadLocalTokenCache::ThreadLocalTokenCache(size_t capacity) : capacity_(capacity) {}

std::optional<ThreadLocalTokenCache::CachedToken> ThreadLocalTokenCache::get(
    std::string_view token) {
    std::string token_str(token);
    auto it = cache_.find(token_str);

    if (it == cache_.end()) {
        return std::nullopt;  // Cache miss
    }

    // Move to front (LRU update)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);

    return it->second->second;
}

void ThreadLocalTokenCache::put(std::string_view token, JwtClaims claims) {
    std::string token_str(token);

    // Check if already exists
    auto it = cache_.find(token_str);
    if (it != cache_.end()) {
        // Update existing entry
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        it->second->second.claims = std::move(claims);
        it->second->second.cached_at = std::time(nullptr);
        return;
    }

    // Evict oldest if at capacity
    if (cache_.size() >= capacity_) {
        auto& oldest = lru_list_.back();
        cache_.erase(oldest.first);
        lru_list_.pop_back();
    }

    // Insert new entry at front
    CachedToken cached{std::move(claims), std::time(nullptr)};
    lru_list_.emplace_front(token_str, std::move(cached));
    cache_[token_str] = lru_list_.begin();
}

void ThreadLocalTokenCache::clear() {
    cache_.clear();
    lru_list_.clear();
}

// ============================================================================
// JwtValidator Implementation
// ============================================================================

JwtValidator::JwtValidator(JwtValidatorConfig config) : config_(std::move(config)) {
    if (config_.cache_enabled) {
        cache_ = std::make_unique<ThreadLocalTokenCache>(config_.cache_capacity);
    }
}

ValidationResult JwtValidator::validate(std::string_view token) {
    // Check cache first
    if (cache_) {
        auto cached = cache_->get(token);
        if (cached) {
            // Validate expiration (don't serve expired tokens from cache)
            auto now = std::time(nullptr);
            if (cached->claims.exp > 0 && cached->claims.exp < now) {
                return ValidationResult::failure("Token expired");
            }
            return ValidationResult::success(cached->claims);
        }
    }

    // Cache miss - full validation
    auto result = validate_uncached(token);

    // Cache successful validations
    if (result.valid && cache_) {
        cache_->put(token, result.claims);
    }

    return result;
}

ValidationResult JwtValidator::validate_uncached(std::string_view token) {
    // STEP 1: Split token into header.payload.signature
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '.') {
            parts.push_back(token.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < token.size()) {
        parts.push_back(token.substr(start));
    }

    if (parts.size() != 3) {
        return ValidationResult::failure("Malformed JWT");
    }

    // STEP 2: Base64url decode header and payload
    auto header_json = base64url_decode(parts[0]);
    if (!header_json) {
        return ValidationResult::failure("Invalid header encoding");
    }

    auto payload_json = base64url_decode(parts[1]);
    if (!payload_json) {
        return ValidationResult::failure("Invalid payload encoding");
    }

    auto signature_bytes = base64url_decode(parts[2]);
    if (!signature_bytes) {
        return ValidationResult::failure("Invalid signature encoding");
    }

    // STEP 3: Parse header to determine algorithm
    auto header = JwtHeader::parse(*header_json);
    if (!header) {
        return ValidationResult::failure("Invalid header format");
    }

    // Reject "none" algorithm (security)
    if (header->algorithm == JwtAlgorithm::None) {
        return ValidationResult::failure("Algorithm 'none' not allowed");
    }

    // STEP 4: Get verification key
    if (!keys_) {
        return ValidationResult::failure("No key manager configured");
    }

    const VerificationKey* key = keys_->get_key(header->algorithm, header->key_id);
    if (!key) {
        return ValidationResult::failure("Unknown key ID");
    }

    // STEP 5: Verify signature
    std::string_view message = token.substr(0, parts[0].size() + 1 + parts[1].size());
    bool sig_valid = verify_signature(header->algorithm, message, *signature_bytes, key);
    if (!sig_valid) {
        return ValidationResult::failure("Invalid signature");
    }

    // STEP 6: Parse and validate claims
    auto claims = JwtClaims::parse(*payload_json);
    if (!claims) {
        return ValidationResult::failure("Invalid claims format");
    }

    return validate_claims(*claims);
}

bool JwtValidator::verify_signature(JwtAlgorithm alg, std::string_view message,
                                     std::string_view signature, const VerificationKey* key) {
    if (!key) {
        return false;
    }

    switch (alg) {
        case JwtAlgorithm::RS256: {
            // RSA-SHA256 verification
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) {
                return false;
            }

            bool valid = false;
            if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key->public_key) == 1) {
                if (EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) == 1) {
                    if (EVP_DigestVerifyFinal(
                            ctx, reinterpret_cast<const unsigned char*>(signature.data()),
                            signature.size()) == 1) {
                        valid = true;
                    }
                }
            }

            EVP_MD_CTX_free(ctx);
            return valid;
        }

        case JwtAlgorithm::ES256: {
            // ECDSA-SHA256 verification
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            if (!ctx) {
                return false;
            }

            bool valid = false;
            if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key->public_key) == 1) {
                if (EVP_DigestVerifyUpdate(ctx, message.data(), message.size()) == 1) {
                    if (EVP_DigestVerifyFinal(
                            ctx, reinterpret_cast<const unsigned char*>(signature.data()),
                            signature.size()) == 1) {
                        valid = true;
                    }
                }
            }

            EVP_MD_CTX_free(ctx);
            return valid;
        }

        case JwtAlgorithm::HS256: {
            // HMAC-SHA256 verification
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int digest_len = 0;

            HMAC(EVP_sha256(), key->hmac_secret.data(), static_cast<int>(key->hmac_secret.size()),
                 reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest,
                 &digest_len);

            // Constant-time comparison (security)
            if (digest_len != signature.size()) {
                return false;
            }
            return CRYPTO_memcmp(digest, signature.data(), digest_len) == 0;
        }

        default:
            return false;
    }
}

ValidationResult JwtValidator::validate_claims(const JwtClaims& claims) {
    auto now = std::time(nullptr);

    // Check expiration (exp) - with clock skew tolerance
    if (claims.exp > 0) {
        if (claims.exp + config_.clock_skew_seconds < now) {
            return ValidationResult::failure("Token expired");
        }
    } else if (config_.require_exp) {
        return ValidationResult::failure("Missing exp claim");
    }

    // Check not-before (nbf) - with clock skew tolerance
    if (claims.nbf > 0) {
        if (claims.nbf - config_.clock_skew_seconds > now) {
            return ValidationResult::failure("Token not yet valid");
        }
    }

    // Check issuer (iss)
    if (!config_.allowed_issuers.empty()) {
        if (claims.iss.empty()) {
            return ValidationResult::failure("Missing iss claim");
        }
        bool valid_issuer = false;
        for (const auto& allowed : config_.allowed_issuers) {
            if (claims.iss == allowed) {
                valid_issuer = true;
                break;
            }
        }
        if (!valid_issuer) {
            return ValidationResult::failure("Invalid issuer");
        }
    }

    // Check audience (aud)
    if (!config_.allowed_audiences.empty()) {
        if (claims.aud.empty()) {
            return ValidationResult::failure("Missing aud claim");
        }
        bool valid_audience = false;
        for (const auto& allowed : config_.allowed_audiences) {
            if (claims.aud == allowed) {
                valid_audience = true;
                break;
            }
        }
        if (!valid_audience) {
            return ValidationResult::failure("Invalid audience");
        }
    }

    // Check subject (sub) if required
    if (config_.require_sub && claims.sub.empty()) {
        return ValidationResult::failure("Missing sub claim");
    }

    return ValidationResult::success(claims);
}

}  // namespace titan::core
