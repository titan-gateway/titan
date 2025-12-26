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

// Titan WebSocket - Implementation
// WebSocket protocol support (RFC 6455)

#include "websocket.hpp"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstring>

#include "http.hpp"
#include "simd.hpp"

namespace titan::http {

namespace {

/// Magic GUID for WebSocket handshake (RFC 6455 §4.2.2)
constexpr std::string_view WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// Base64 encode binary data
std::string base64_encode(const unsigned char* data, size_t length) {
    BIO *bio, *b64;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);  // No newlines
    BIO_write(bio, data, static_cast<int>(length));
    BIO_flush(bio);

    // Get the encoded data using BIO_get_mem_data (OpenSSL 3.x compatible)
    char* encoded_data;
    long encoded_length = BIO_get_mem_data(bio, &encoded_data);

    std::string result(encoded_data, encoded_length);

    BIO_free_all(bio);

    return result;
}

/// Check if string contains another string (case-insensitive for header values)
bool contains_ci(std::string_view haystack, std::string_view needle) {
    auto it =
        std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                    [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); });
    return it != haystack.end();
}

}  // namespace

// ========================================
// WebSocketUtils Implementation
// ========================================

std::string WebSocketUtils::compute_accept_key(std::string_view sec_websocket_key) {
    // RFC 6455 §4.2.2:
    // Sec-WebSocket-Accept = Base64(SHA1(Sec-WebSocket-Key + GUID))

    // Concatenate key + GUID
    std::string concat;
    concat.reserve(sec_websocket_key.size() + WEBSOCKET_GUID.size());
    concat.append(sec_websocket_key);
    concat.append(WEBSOCKET_GUID);

    // SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(concat.data()), concat.size(), hash);

    // Base64 encode
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

bool WebSocketUtils::is_valid_upgrade_request(const http::Request& request) {
    // RFC 6455 §4.2.1: Client handshake requirements

    // 1. Must be GET request
    if (request.method != http::Method::GET) {
        return false;
    }

    // 2. Must have Upgrade: websocket
    std::string upgrade = std::string(request.get_header("Upgrade"));
    if (upgrade.empty()) {
        return false;
    }
    // Case-insensitive comparison
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);
    if (upgrade != "websocket") {
        return false;
    }

    // 3. Must have Connection: Upgrade (case-insensitive, may have other values)
    std::string connection = std::string(request.get_header("Connection"));
    if (!contains_ci(connection, "upgrade")) {
        return false;
    }

    // 4. Must have Sec-WebSocket-Key (base64-encoded 16-byte nonce)
    std::string key = std::string(request.get_header("Sec-WebSocket-Key"));
    if (key.empty()) {
        return false;
    }

    // 5. Must have Sec-WebSocket-Version: 13
    std::string version = std::string(request.get_header("Sec-WebSocket-Version"));
    if (version != "13") {
        return false;  // Only version 13 is supported (RFC 6455)
    }

    return true;
}

std::string WebSocketUtils::create_upgrade_response(std::string_view accept_key,
                                                    std::string_view protocol) {
    // RFC 6455 §4.2.2: Server handshake response
    std::string response;
    response.reserve(256);

    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: ";
    response += accept_key;
    response += "\r\n";

    // Optional: Sec-WebSocket-Protocol (if subprotocol negotiated)
    if (!protocol.empty()) {
        response += "Sec-WebSocket-Protocol: ";
        response += protocol;
        response += "\r\n";
    }

    response += "\r\n";  // End of headers

    return response;
}

void WebSocketUtils::unmask_payload(std::span<uint8_t> payload, uint32_t masking_key) {
    // RFC 6455 §5.3: Masking algorithm
    // transformed-octet-i = original-octet-i XOR masking-key-octet-(i % 4)

    // Use SIMD-accelerated unmasking (AVX2/SSE2/NEON)
    // Performance: ~30x faster than scalar for large payloads
    simd::unmask_payload(payload.data(), payload.size(), masking_key);
}

std::vector<uint8_t> WebSocketUtils::create_close_frame(uint16_t status_code,
                                                        std::string_view reason) {
    // RFC 6455 §5.5.1: Close frame format
    std::vector<uint8_t> frame;

    // Payload: 2-byte status code + optional UTF-8 reason
    size_t payload_len = 2 + reason.size();

    encode_frame_header(frame, true, WebSocketOpcode::CLOSE, false, payload_len, 0);

    // Status code (big-endian)
    frame.push_back(static_cast<uint8_t>(status_code >> 8));
    frame.push_back(static_cast<uint8_t>(status_code & 0xFF));

    // Reason (UTF-8)
    if (!reason.empty()) {
        frame.insert(frame.end(), reason.begin(), reason.end());
    }

    return frame;
}

std::vector<uint8_t> WebSocketUtils::create_pong_frame(std::span<const uint8_t> ping_payload) {
    // RFC 6455 §5.5.3: Pong frame must echo Ping payload
    std::vector<uint8_t> frame;

    encode_frame_header(frame, true, WebSocketOpcode::PONG, false, ping_payload.size(), 0);

    // Copy ping payload
    frame.insert(frame.end(), ping_payload.begin(), ping_payload.end());

    return frame;
}

std::vector<uint8_t> WebSocketUtils::create_ping_frame() {
    // RFC 6455 §5.5.2: Ping frame (typically no payload)
    std::vector<uint8_t> frame;

    encode_frame_header(frame, true, WebSocketOpcode::PING, false, 0, 0);

    return frame;
}

void WebSocketUtils::encode_frame_header(std::vector<uint8_t>& buffer, bool fin, uint8_t opcode,
                                         bool mask, uint64_t payload_length, uint32_t masking_key) {
    // RFC 6455 §5.2: Frame header format

    // Byte 0: FIN (1 bit) + RSV1-3 (3 bits) + Opcode (4 bits)
    uint8_t byte0 = (fin ? 0x80 : 0x00) | (opcode & 0x0F);
    buffer.push_back(byte0);

    // Byte 1: MASK (1 bit) + Payload length (7 bits)
    uint8_t byte1 = mask ? 0x80 : 0x00;

    if (payload_length <= 125) {
        // Short length (0-125)
        byte1 |= static_cast<uint8_t>(payload_length);
        buffer.push_back(byte1);
    } else if (payload_length <= 0xFFFF) {
        // Medium length (126 = 16-bit extended length)
        byte1 |= 126;
        buffer.push_back(byte1);
        buffer.push_back(static_cast<uint8_t>(payload_length >> 8));
        buffer.push_back(static_cast<uint8_t>(payload_length & 0xFF));
    } else {
        // Long length (127 = 64-bit extended length)
        byte1 |= 127;
        buffer.push_back(byte1);
        for (int i = 7; i >= 0; --i) {
            buffer.push_back(static_cast<uint8_t>((payload_length >> (i * 8)) & 0xFF));
        }
    }

    // Masking key (4 bytes, only if MASK=1)
    if (mask) {
        buffer.push_back(static_cast<uint8_t>(masking_key >> 24));
        buffer.push_back(static_cast<uint8_t>(masking_key >> 16));
        buffer.push_back(static_cast<uint8_t>(masking_key >> 8));
        buffer.push_back(static_cast<uint8_t>(masking_key));
    }
}

// ========================================
// WebSocketFrameParser Implementation
// ========================================

WebSocketFrameParser::ParseResult WebSocketFrameParser::parse(std::span<const uint8_t> data,
                                                              WebSocketFrame& out_frame,
                                                              size_t& consumed) {
    consumed = 0;
    size_t data_offset = 0;

    while (true) {
        switch (state_) {
            case State::ReadHeader: {
                // Need at least 2 bytes for header
                size_t needed = 2 - buffer_.size();
                size_t available = data.size() - data_offset;
                size_t to_copy = std::min(needed, available);

                buffer_.insert(buffer_.end(), data.begin() + data_offset,
                               data.begin() + data_offset + to_copy);
                data_offset += to_copy;

                if (buffer_.size() < 2) {
                    consumed = data_offset;
                    return ParseResult::Incomplete;  // Need more data
                }

                // Parse first 2 bytes
                uint8_t byte0 = buffer_[0];
                uint8_t byte1 = buffer_[1];

                fin_ = (byte0 & 0x80) != 0;
                opcode_ = byte0 & 0x0F;
                masked_ = (byte1 & 0x80) != 0;
                uint8_t payload_len = byte1 & 0x7F;

                // Validate opcode
                if (opcode_ > 0x2 && opcode_ < 0x8) {
                    return ParseResult::Error;  // Reserved data opcode
                }
                if (opcode_ > 0xA) {
                    return ParseResult::Error;  // Reserved control opcode
                }

                // Control frames must not be fragmented
                if (opcode_ >= 0x8 && !fin_) {
                    return ParseResult::Error;  // Fragmented control frame
                }

                // Control frames must have payload <= 125 bytes
                if (opcode_ >= 0x8 && payload_len > 125) {
                    return ParseResult::Error;  // Control frame too large
                }

                header_size_ = 2;

                // Determine payload length
                if (payload_len <= 125) {
                    payload_length_ = payload_len;
                    state_ = masked_ ? State::ReadMaskingKey : State::ReadPayload;
                } else if (payload_len == 126) {
                    state_ = State::ReadExtendedLen16;
                    header_size_ += 2;
                } else {  // 127
                    state_ = State::ReadExtendedLen64;
                    header_size_ += 8;
                }

                break;
            }

            case State::ReadExtendedLen16: {
                // Need 2 more bytes for 16-bit length
                size_t needed = 4 - buffer_.size();  // Total header is now 4 bytes
                size_t available = data.size() - data_offset;
                size_t to_copy = std::min(needed, available);

                buffer_.insert(buffer_.end(), data.begin() + data_offset,
                               data.begin() + data_offset + to_copy);
                data_offset += to_copy;

                if (buffer_.size() < 4) {
                    consumed = data_offset;
                    return ParseResult::Incomplete;
                }

                // Read 16-bit big-endian length
                payload_length_ = (static_cast<uint64_t>(buffer_[2]) << 8) | buffer_[3];

                state_ = masked_ ? State::ReadMaskingKey : State::ReadPayload;
                break;
            }

            case State::ReadExtendedLen64: {
                // Need 8 more bytes for 64-bit length
                size_t needed = 10 - buffer_.size();  // Total header is now 10 bytes
                size_t available = data.size() - data_offset;
                size_t to_copy = std::min(needed, available);

                buffer_.insert(buffer_.end(), data.begin() + data_offset,
                               data.begin() + data_offset + to_copy);
                data_offset += to_copy;

                if (buffer_.size() < 10) {
                    consumed = data_offset;
                    return ParseResult::Incomplete;
                }

                // Read 64-bit big-endian length
                payload_length_ = 0;
                for (int i = 0; i < 8; ++i) {
                    payload_length_ = (payload_length_ << 8) | buffer_[2 + i];
                }

                // Sanity check: most significant bit must be 0 (RFC 6455 §5.2)
                if (payload_length_ & (1ULL << 63)) {
                    return ParseResult::Error;  // Invalid payload length
                }

                state_ = masked_ ? State::ReadMaskingKey : State::ReadPayload;
                break;
            }

            case State::ReadMaskingKey: {
                // Need 4 bytes for masking key
                size_t needed = header_size_ + 4 - buffer_.size();
                size_t available = data.size() - data_offset;
                size_t to_copy = std::min(needed, available);

                buffer_.insert(buffer_.end(), data.begin() + data_offset,
                               data.begin() + data_offset + to_copy);
                data_offset += to_copy;

                if (buffer_.size() < header_size_ + 4) {
                    consumed = data_offset;
                    return ParseResult::Incomplete;
                }

                // Read masking key
                size_t key_offset = header_size_;
                masking_key_ = (static_cast<uint32_t>(buffer_[key_offset]) << 24) |
                               (static_cast<uint32_t>(buffer_[key_offset + 1]) << 16) |
                               (static_cast<uint32_t>(buffer_[key_offset + 2]) << 8) |
                               buffer_[key_offset + 3];

                header_size_ += 4;
                state_ = State::ReadPayload;
                break;
            }

            case State::ReadPayload: {
                // Read payload data
                size_t total_frame_size = header_size_ + payload_length_;
                size_t needed = total_frame_size - buffer_.size();

                if (needed > 0) {
                    // Need more data for payload
                    if (data_offset >= data.size()) {
                        // No more input data available
                        consumed = data_offset;
                        return ParseResult::Incomplete;
                    }

                    size_t available = data.size() - data_offset;
                    size_t to_copy = std::min(needed, available);

                    buffer_.insert(buffer_.end(), data.begin() + data_offset,
                                   data.begin() + data_offset + to_copy);
                    data_offset += to_copy;

                    if (buffer_.size() < total_frame_size) {
                        consumed = data_offset;
                        return ParseResult::Incomplete;
                    }
                }

                // Frame complete! Build output frame
                out_frame.fin = fin_;
                out_frame.opcode = opcode_;
                out_frame.masked = masked_;
                out_frame.masking_key = masking_key_;
                out_frame.payload_length = payload_length_;

                // Payload is everything after header
                if (payload_length_ > 0) {
                    out_frame.payload =
                        std::span<const uint8_t>(buffer_.data() + header_size_, payload_length_);
                } else {
                    out_frame.payload = std::span<const uint8_t>();
                }

                consumed = data_offset;
                state_ = State::Complete;
                return ParseResult::Complete;
            }

            case State::Complete: {
                // Should not reach here (caller should reset parser after getting Complete)
                return ParseResult::Error;
            }
        }

        // Check if we should exit loop
        if (data_offset >= data.size() && state_ != State::ReadPayload) {
            consumed = data_offset;
            return ParseResult::Incomplete;
        }
    }
}

void WebSocketFrameParser::reset() {
    state_ = State::ReadHeader;
    buffer_.clear();
    fin_ = false;
    opcode_ = 0;
    masked_ = false;
    payload_length_ = 0;
    masking_key_ = 0;
    header_size_ = 0;
}

const char* WebSocketFrameParser::state_name() const noexcept {
    switch (state_) {
        case State::ReadHeader:
            return "ReadHeader";
        case State::ReadExtendedLen16:
            return "ReadExtendedLen16";
        case State::ReadExtendedLen64:
            return "ReadExtendedLen64";
        case State::ReadMaskingKey:
            return "ReadMaskingKey";
        case State::ReadPayload:
            return "ReadPayload";
        case State::Complete:
            return "Complete";
    }
    return "Unknown";
}

}  // namespace titan::http
