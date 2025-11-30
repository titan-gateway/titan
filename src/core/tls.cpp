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

// Titan TLS - Implementation
// TLS/SSL utilities for secure connections with ALPN support

#include "tls.hpp"

#include <openssl/evp.h>

#include <cstring>
#include <format>

namespace titan::core {

// ============================
// Error Handling
// ============================

std::string TlsErrorCategory::message(int ev) const {
    char buf[256];
    ERR_error_string_n(static_cast<unsigned long>(ev), buf, sizeof(buf));
    return std::string(buf);
}

const TlsErrorCategory& tls_category() noexcept {
    static TlsErrorCategory instance;
    return instance;
}

std::error_code make_tls_error() noexcept {
    unsigned long err = ERR_get_error();
    if (err == 0) {
        // No error in queue - return generic TLS error
        return std::error_code(1, tls_category());
    }
    return std::error_code(static_cast<int>(err), tls_category());
}

// ============================
// ALPN Callback
// ============================

/// ALPN server callback
/// This is called during TLS handshake to select the protocol
static int alpn_select_callback(SSL* ssl, const unsigned char** out, unsigned char* outlen,
                                const unsigned char* in, unsigned int inlen, void* arg) {
    (void)ssl;  // Unused

    // Get supported protocols from context user data
    auto* supported = static_cast<std::vector<std::string>*>(arg);

    // Parse client's list of protocols
    const unsigned char* client_proto = in;
    const unsigned char* client_proto_end = in + inlen;

    while (client_proto < client_proto_end) {
        unsigned char proto_len = *client_proto++;
        if (client_proto + proto_len > client_proto_end) {
            break;  // Malformed
        }

        std::string_view client_protocol(reinterpret_cast<const char*>(client_proto), proto_len);

        // Check if we support this protocol
        for (const auto& supported_proto : *supported) {
            if (client_protocol == supported_proto) {
                // Match found - select this protocol
                *out = client_proto;
                *outlen = proto_len;
                return SSL_TLSEXT_ERR_OK;
            }
        }

        client_proto += proto_len;
    }

    // No match found
    return SSL_TLSEXT_ERR_NOACK;
}

// ============================
// TLS Context
// ============================

std::optional<TlsContext> TlsContext::create(std::string_view cert_path, std::string_view key_path,
                                             std::span<const std::string> alpn_protocols,
                                             std::error_code& error_out) {
    // Create SSL context (TLS 1.2+)
    SslCtxPtr ctx(SSL_CTX_new(TLS_server_method()));
    if (!ctx) {
        error_out = make_tls_error();
        return std::nullopt;
    }

    // Set minimum TLS version (TLS 1.2)
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);

    // Load certificate
    if (SSL_CTX_use_certificate_file(ctx.get(), cert_path.data(), SSL_FILETYPE_PEM) <= 0) {
        error_out = make_tls_error();
        return std::nullopt;
    }

    // Load private key
    if (SSL_CTX_use_PrivateKey_file(ctx.get(), key_path.data(), SSL_FILETYPE_PEM) <= 0) {
        error_out = make_tls_error();
        return std::nullopt;
    }

    // Verify private key matches certificate
    if (!SSL_CTX_check_private_key(ctx.get())) {
        error_out = make_tls_error();
        return std::nullopt;
    }

    // Store ALPN protocols (if any) with stable address
    auto alpn_storage =
        std::make_unique<std::vector<std::string>>(alpn_protocols.begin(), alpn_protocols.end());

    // Configure ALPN callback if protocols specified
    if (!alpn_storage->empty()) {
        // Use raw pointer for callback - the unique_ptr keeps it alive
        SSL_CTX_set_alpn_select_cb(ctx.get(), alpn_select_callback, alpn_storage.get());
    }

    // Enable session resumption for better performance
    SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_SERVER);

    return TlsContext(std::move(ctx), std::move(alpn_storage));
}

SslPtr TlsContext::create_ssl(int sockfd) const {
    SslPtr ssl(SSL_new(ctx_.get()));
    if (!ssl) {
        return nullptr;
    }

    // Attach socket to SSL object
    SSL_set_fd(ssl.get(), sockfd);

    // Set server mode
    SSL_set_accept_state(ssl.get());

    return ssl;
}

// ============================
// TLS Operations
// ============================

TlsHandshakeResult ssl_accept_nonblocking(SSL* ssl) noexcept {
    ERR_clear_error();  // Clear error queue before operation

    int result = SSL_accept(ssl);

    if (result == 1) {
        // Handshake completed successfully
        return TlsHandshakeResult::Complete;
    }

    int err = SSL_get_error(ssl, result);

    switch (err) {
        case SSL_ERROR_WANT_READ:
            return TlsHandshakeResult::WantRead;

        case SSL_ERROR_WANT_WRITE:
            return TlsHandshakeResult::WantWrite;

        default:
            // Fatal error
            return TlsHandshakeResult::Error;
    }
}

std::string_view get_alpn_protocol(SSL* ssl) noexcept {
    const unsigned char* data = nullptr;
    unsigned int len = 0;

    SSL_get0_alpn_selected(ssl, &data, &len);

    if (data == nullptr || len == 0) {
        return "";
    }

    return std::string_view(reinterpret_cast<const char*>(data), len);
}

int ssl_read_nonblocking(SSL* ssl, std::span<uint8_t> buffer) noexcept {
    ERR_clear_error();
    return SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
}

int ssl_write_nonblocking(SSL* ssl, std::span<const uint8_t> data) noexcept {
    ERR_clear_error();
    return SSL_write(ssl, data.data(), static_cast<int>(data.size()));
}

// ============================
// OpenSSL Initialization
// ============================

void initialize_openssl() noexcept {
    // OpenSSL 1.1.0+ auto-initializes, but we call this for compatibility
    OPENSSL_init_ssl(0, nullptr);
}

void cleanup_openssl() noexcept {
    // OpenSSL 1.1.0+ auto-cleans up, but we call this for completeness
    EVP_cleanup();
}

}  // namespace titan::core
