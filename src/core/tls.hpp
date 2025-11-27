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


// Titan TLS - Header
// TLS/SSL utilities for secure connections with ALPN support

#pragma once

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace titan::core {

/// TLS error category for std::error_code
class TlsErrorCategory : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override {
        return "tls";
    }

    [[nodiscard]] std::string message(int ev) const override;
};

/// Get TLS error category instance
[[nodiscard]] const TlsErrorCategory& tls_category() noexcept;

/// Create error_code from current OpenSSL error queue
[[nodiscard]] std::error_code make_tls_error() noexcept;

/// SSL_CTX deleter for std::unique_ptr
struct SslCtxDeleter {
    void operator()(SSL_CTX* ctx) const noexcept {
        if (ctx) {
            SSL_CTX_free(ctx);
        }
    }
};

/// SSL deleter for std::unique_ptr
struct SslDeleter {
    void operator()(SSL* ssl) const noexcept {
        if (ssl) {
            SSL_free(ssl);
        }
    }
};

/// Unique pointer types for SSL objects
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;
using SslPtr = std::unique_ptr<SSL, SslDeleter>;

/// TLS configuration and context management
class TlsContext {
public:
    /// Create TLS context with certificate and private key
    /// @param cert_path Path to certificate file (PEM format)
    /// @param key_path Path to private key file (PEM format)
    /// @param alpn_protocols List of ALPN protocol names (e.g., "h2", "http/1.1")
    /// @param error_out Output parameter for error code
    /// @return TlsContext or nullopt on error
    [[nodiscard]] static std::optional<TlsContext>
    create(std::string_view cert_path,
           std::string_view key_path,
           std::span<const std::string> alpn_protocols,
           std::error_code& error_out);

    /// Create server-side SSL connection object
    [[nodiscard]] SslPtr create_ssl(int sockfd) const;

    /// Get underlying SSL_CTX pointer (for advanced use)
    [[nodiscard]] SSL_CTX* native_handle() const noexcept {
        return ctx_.get();
    }

    // Movable but not copyable
    TlsContext(TlsContext&&) = default;
    TlsContext& operator=(TlsContext&&) = default;
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

private:
    explicit TlsContext(SslCtxPtr ctx, std::unique_ptr<std::vector<std::string>> alpn)
        : ctx_(std::move(ctx)), alpn_protocols_(std::move(alpn)) {}

    SslCtxPtr ctx_;
    std::unique_ptr<std::vector<std::string>> alpn_protocols_;  // Stable address for ALPN callback
};

/// TLS handshake result
enum class TlsHandshakeResult {
    Complete,     // Handshake completed successfully
    WantRead,     // Need more data from socket (call again after read)
    WantWrite,    // Need to write data to socket (call again after write)
    Error,        // Fatal error occurred
};

/// Perform TLS server handshake (non-blocking)
/// @param ssl SSL connection object
/// @return Handshake result
[[nodiscard]] TlsHandshakeResult ssl_accept_nonblocking(SSL* ssl) noexcept;

/// Get negotiated ALPN protocol
/// @param ssl SSL connection object after successful handshake
/// @return Protocol name (e.g., "h2", "http/1.1") or empty if no ALPN
[[nodiscard]] std::string_view get_alpn_protocol(SSL* ssl) noexcept;

/// Read data from TLS connection (non-blocking)
/// @param ssl SSL connection object
/// @param buffer Buffer to read into
/// @return Bytes read, or negative on error (check SSL_get_error)
[[nodiscard]] int ssl_read_nonblocking(SSL* ssl, std::span<uint8_t> buffer) noexcept;

/// Write data to TLS connection (non-blocking)
/// @param ssl SSL connection object
/// @param data Data to write
/// @return Bytes written, or negative on error (check SSL_get_error)
[[nodiscard]] int ssl_write_nonblocking(SSL* ssl, std::span<const uint8_t> data) noexcept;

/// Initialize OpenSSL library (call once at startup)
void initialize_openssl() noexcept;

/// Cleanup OpenSSL library (call once at shutdown)
void cleanup_openssl() noexcept;

} // namespace titan::core
