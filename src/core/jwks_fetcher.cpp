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

// Titan JWKS Fetcher - Implementation
// Fetch JWT verification keys from JWKS endpoints (Auth0, Keycloak, etc.)

#include "jwks_fetcher.hpp"

#include <httplib.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <nlohmann/json.hpp>

namespace titan::core {

// JsonWebKey implementation

std::optional<JsonWebKey> JsonWebKey::parse(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);

        JsonWebKey jwk;
        jwk.kty = j.value("kty", "");
        jwk.alg = j.value("alg", "");
        jwk.kid = j.value("kid", "");
        jwk.use = j.value("use", "sig");  // Default to signature

        if (jwk.kty == "RSA") {
            jwk.n = j.value("n", "");
            jwk.e = j.value("e", "");
        } else if (jwk.kty == "EC") {
            jwk.crv = j.value("crv", "");
            jwk.x = j.value("x", "");
            jwk.y = j.value("y", "");
        }

        return jwk;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

// JwksFetcher implementation

JwksFetcher::JwksFetcher(JwksConfig config) : config_(std::move(config)) {
    // Initialize with empty key manager
    keys_.store(std::make_shared<KeyManager>());
}

JwksFetcher::~JwksFetcher() {
    stop();
}

void JwksFetcher::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    // Fetch keys immediately on start
    fetch_keys();

    // Start background thread
    fetch_thread_ = std::make_unique<std::thread>(&JwksFetcher::fetch_loop, this);
}

void JwksFetcher::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }

    if (fetch_thread_ && fetch_thread_->joinable()) {
        fetch_thread_->join();
    }
}

std::shared_ptr<KeyManager> JwksFetcher::get_keys() const {
    return keys_.load();
}

void JwksFetcher::fetch_loop() {
    while (running_) {
        // Sleep for refresh interval
        for (uint32_t i = 0; i < config_.refresh_interval_seconds && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_)
            break;

        // Fetch keys
        fetch_keys();
    }
}

bool JwksFetcher::fetch_keys() {
    // Fetch JWKS JSON from endpoint
    auto response = http_get(config_.url);
    if (!response.has_value()) {
        consecutive_failures_++;

        // Update state based on failures
        if (consecutive_failures_ >= config_.retry_max) {
            state_.store(JwksFetcherState::CircuitOpen);
        } else {
            state_.store(JwksFetcherState::Degraded);
        }

        return false;
    }

    // Parse JWKS JSON
    auto jwks = parse_jwks(*response);
    if (!jwks.has_value() || jwks->empty()) {
        consecutive_failures_++;
        return false;
    }

    // Convert JWKs to VerificationKeys
    auto new_keys = std::make_shared<KeyManager>();
    size_t successful_conversions = 0;

    for (const auto& jwk : *jwks) {
        auto key = jwk_to_verification_key(jwk);
        if (key.has_value()) {
            new_keys->add_key(std::move(*key));
            successful_conversions++;
        }
    }

    if (successful_conversions == 0) {
        consecutive_failures_++;
        return false;
    }

    // RCU: Atomic pointer swap
    keys_.store(new_keys);

    // Update state
    consecutive_failures_ = 0;
    last_success_ts_ = std::time(nullptr);
    state_.store(JwksFetcherState::Healthy);

    return true;
}

std::optional<std::string> JwksFetcher::http_get(const std::string& url) {
    try {
        // Parse URL to extract host and path
        httplib::Client client(url.substr(0, url.find('/', 8)));  // Extract base URL
        client.set_connection_timeout(config_.timeout_seconds, 0);
        client.set_read_timeout(config_.timeout_seconds, 0);

        std::string path = url.substr(url.find('/', 8));  // Extract path after protocol://host
        if (path.empty()) {
            path = "/";
        }

        auto res = client.Get(path);
        if (!res || res->status != 200) {
            return std::nullopt;
        }

        return res->body;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<JsonWebKey>> JwksFetcher::parse_jwks(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);

        // JWKS format: { "keys": [ {...}, {...} ] }
        if (!j.contains("keys") || !j["keys"].is_array()) {
            return std::nullopt;
        }

        std::vector<JsonWebKey> jwks;
        for (const auto& jwk_json : j["keys"]) {
            auto jwk = JsonWebKey::parse(jwk_json.dump());
            if (jwk.has_value()) {
                jwks.push_back(std::move(*jwk));
            }
        }

        return jwks;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<VerificationKey> JwksFetcher::jwk_to_verification_key(const JsonWebKey& jwk) {
    // Determine algorithm
    auto alg = parse_algorithm(jwk.alg);
    if (!alg.has_value()) {
        return std::nullopt;
    }

    VerificationKey key;
    key.algorithm = *alg;
    key.key_id = jwk.kid;

    // Convert based on key type
    if (jwk.kty == "RSA") {
        key.public_key = rsa_jwk_to_evp_pkey(jwk);
    } else if (jwk.kty == "EC") {
        key.public_key = ec_jwk_to_evp_pkey(jwk);
    } else {
        return std::nullopt;
    }

    if (key.public_key == nullptr) {
        return std::nullopt;
    }

    return key;
}

EVP_PKEY* JwksFetcher::rsa_jwk_to_evp_pkey(const JsonWebKey& jwk) {
    // Decode base64url n and e
    auto n_bin = base64url_decode(jwk.n);
    auto e_bin = base64url_decode(jwk.e);

    if (!n_bin.has_value() || !e_bin.has_value()) {
        return nullptr;
    }

    // Convert to BIGNUM
    BIGNUM* n_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(n_bin->data()),
                             static_cast<int>(n_bin->size()), nullptr);
    BIGNUM* e_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(e_bin->data()),
                             static_cast<int>(e_bin->size()), nullptr);

    if (n_bn == nullptr || e_bn == nullptr) {
        BN_free(n_bn);
        BN_free(e_bn);
        return nullptr;
    }

    // Create RSA key
    RSA* rsa = RSA_new();
    if (rsa == nullptr) {
        BN_free(n_bn);
        BN_free(e_bn);
        return nullptr;
    }

    // Set RSA parameters (OpenSSL 3.x API)
    if (RSA_set0_key(rsa, n_bn, e_bn, nullptr) != 1) {
        RSA_free(rsa);
        BN_free(n_bn);
        BN_free(e_bn);
        return nullptr;
    }

    // Convert RSA to EVP_PKEY
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (pkey == nullptr) {
        RSA_free(rsa);
        return nullptr;
    }

    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        return nullptr;
    }

    return pkey;
}

EVP_PKEY* JwksFetcher::ec_jwk_to_evp_pkey(const JsonWebKey& jwk) {
    // Only P-256 supported for ES256
    if (jwk.crv != "P-256") {
        return nullptr;
    }

    // Decode base64url x and y
    auto x_bin = base64url_decode(jwk.x);
    auto y_bin = base64url_decode(jwk.y);

    if (!x_bin.has_value() || !y_bin.has_value()) {
        return nullptr;
    }

    // Convert to BIGNUM
    BIGNUM* x_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(x_bin->data()),
                             static_cast<int>(x_bin->size()), nullptr);
    BIGNUM* y_bn = BN_bin2bn(reinterpret_cast<const unsigned char*>(y_bin->data()),
                             static_cast<int>(y_bin->size()), nullptr);

    if (x_bn == nullptr || y_bn == nullptr) {
        BN_free(x_bn);
        BN_free(y_bn);
        return nullptr;
    }

    // Create EC key with P-256 curve
    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ec_key == nullptr) {
        BN_free(x_bn);
        BN_free(y_bn);
        return nullptr;
    }

    // Create point on curve
    const EC_GROUP* group = EC_KEY_get0_group(ec_key);
    EC_POINT* point = EC_POINT_new(group);
    if (point == nullptr) {
        EC_KEY_free(ec_key);
        BN_free(x_bn);
        BN_free(y_bn);
        return nullptr;
    }

    // Set point coordinates
    if (EC_POINT_set_affine_coordinates(group, point, x_bn, y_bn, nullptr) != 1) {
        EC_POINT_free(point);
        EC_KEY_free(ec_key);
        BN_free(x_bn);
        BN_free(y_bn);
        return nullptr;
    }

    // Set public key
    if (EC_KEY_set_public_key(ec_key, point) != 1) {
        EC_POINT_free(point);
        EC_KEY_free(ec_key);
        BN_free(x_bn);
        BN_free(y_bn);
        return nullptr;
    }

    EC_POINT_free(point);
    BN_free(x_bn);
    BN_free(y_bn);

    // Convert EC_KEY to EVP_PKEY
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (pkey == nullptr) {
        EC_KEY_free(ec_key);
        return nullptr;
    }

    if (EVP_PKEY_assign_EC_KEY(pkey, ec_key) != 1) {
        EVP_PKEY_free(pkey);
        EC_KEY_free(ec_key);
        return nullptr;
    }

    return pkey;
}

uint32_t JwksFetcher::calculate_backoff(uint32_t attempt) const {
    // Exponential backoff: 2^attempt seconds, capped at circuit_breaker_seconds
    uint32_t backoff = static_cast<uint32_t>(std::pow(2, attempt));
    return std::min(backoff, config_.circuit_breaker_seconds);
}

}  // namespace titan::core
