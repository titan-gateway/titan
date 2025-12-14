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

// Titan Rate Limiting - Header
// Thread-local token bucket algorithm (lock-free, approximate limits)

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "../core/containers.hpp"

namespace titan::gateway {

/// Token bucket for rate limiting (thread-local, no synchronization)
class TokenBucket {
public:
    /// Create a token bucket
    /// @param capacity Maximum number of tokens (burst size)
    /// @param refill_rate Tokens added per second
    TokenBucket(uint64_t capacity, uint64_t refill_rate);

    ~TokenBucket() = default;

    // Non-copyable, movable
    TokenBucket(const TokenBucket&) = delete;
    TokenBucket& operator=(const TokenBucket&) = delete;
    TokenBucket(TokenBucket&& other) noexcept;
    TokenBucket& operator=(TokenBucket&& other) noexcept;

    /// Try to consume N tokens
    /// @param tokens Number of tokens to consume (default 1)
    /// @return true if tokens were consumed, false if rate limit exceeded
    [[nodiscard]] bool consume(uint64_t tokens = 1);

    /// Get current number of available tokens
    [[nodiscard]] uint64_t available() const noexcept { return tokens_; }

    /// Get bucket capacity
    [[nodiscard]] uint64_t capacity() const noexcept { return capacity_; }

    /// Get refill rate (tokens per second)
    [[nodiscard]] uint64_t refill_rate() const noexcept { return refill_rate_; }

    /// Reset the bucket to full capacity
    void reset() noexcept;

private:
    /// Refill tokens based on elapsed time (no atomics needed - thread-local)
    void refill();

    const uint64_t capacity_;     // Maximum tokens (burst size)
    const uint64_t refill_rate_;  // Tokens per second
    uint64_t tokens_;             // Current available tokens (no atomic - thread-local)
    std::chrono::steady_clock::time_point last_refill_;
};

/// Thread-local rate limiter with per-key buckets
/// Each worker thread has its own instance - no synchronization needed
class ThreadLocalRateLimiter {
public:
    /// Create a rate limiter
    /// @param capacity Token bucket capacity (burst size)
    /// @param refill_rate Tokens per second
    ThreadLocalRateLimiter(uint64_t capacity, uint64_t refill_rate);

    ~ThreadLocalRateLimiter() = default;

    // Non-copyable, non-movable
    ThreadLocalRateLimiter(const ThreadLocalRateLimiter&) = delete;
    ThreadLocalRateLimiter& operator=(const ThreadLocalRateLimiter&) = delete;

    /// Check if a request should be allowed for a key (no locks)
    /// @param key Identifier (e.g., IP address, API key)
    /// @param tokens Number of tokens to consume (default 1)
    /// @return true if request is allowed, false if rate limited
    [[nodiscard]] bool allow(std::string_view key, uint64_t tokens = 1);

    /// Reset rate limit for a specific key
    void reset(std::string_view key);

    /// Clear all buckets
    void clear();

    /// Get number of tracked keys
    [[nodiscard]] size_t key_count() const noexcept { return buckets_.size(); }

    /// Get available tokens for a key (capacity if key not found)
    [[nodiscard]] uint64_t available(std::string_view key) const;

private:
    const uint64_t capacity_;
    const uint64_t refill_rate_;
    titan::core::fast_map<std::string, TokenBucket> buckets_;  // Thread-local, no mutex
};

}  // namespace titan::gateway
