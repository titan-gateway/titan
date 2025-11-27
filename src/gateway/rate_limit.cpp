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


// Titan Rate Limiting - Implementation

#include "rate_limit.hpp"

#include <algorithm>

namespace titan::gateway {

// TokenBucket implementation

TokenBucket::TokenBucket(uint64_t capacity, uint64_t refill_rate)
    : capacity_(capacity)
    , refill_rate_(refill_rate)
    , tokens_(capacity)
    , last_refill_(std::chrono::steady_clock::now()) {}

TokenBucket::TokenBucket(TokenBucket&& other) noexcept
    : capacity_(other.capacity_)
    , refill_rate_(other.refill_rate_)
    , tokens_(other.tokens_)
    , last_refill_(other.last_refill_) {}

TokenBucket& TokenBucket::operator=(TokenBucket&& other) noexcept {
    if (this != &other) {
        // capacity_ and refill_rate_ are const, can't reassign
        // This is intentional - buckets with different configs shouldn't be moved
        tokens_ = other.tokens_;
        last_refill_ = other.last_refill_;
    }
    return *this;
}

bool TokenBucket::consume(uint64_t tokens) {
    refill();

    if (tokens_ >= tokens) {
        tokens_ -= tokens;
        return true;  // Successfully consumed
    }

    return false;  // Not enough tokens
}

void TokenBucket::reset() noexcept {
    tokens_ = capacity_;
    last_refill_ = std::chrono::steady_clock::now();
}

void TokenBucket::refill() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);

    if (elapsed.count() <= 0) {
        return;  // No time has passed
    }

    // Calculate tokens to add
    // tokens = (elapsed_ms * refill_rate) / 1000
    uint64_t tokens_to_add = (static_cast<uint64_t>(elapsed.count()) * refill_rate_) / 1000;

    if (tokens_to_add == 0) {
        return;  // Not enough time elapsed to add a token
    }

    // Update timestamp
    last_refill_ = now;

    // Add tokens (capped at capacity)
    tokens_ = std::min(tokens_ + tokens_to_add, capacity_);
}

// ThreadLocalRateLimiter implementation

ThreadLocalRateLimiter::ThreadLocalRateLimiter(uint64_t capacity, uint64_t refill_rate)
    : capacity_(capacity)
    , refill_rate_(refill_rate) {}

bool ThreadLocalRateLimiter::allow(std::string_view key, uint64_t tokens) {
    std::string key_str{key};

    // Find or create bucket for this key (thread-local map)
    auto it = buckets_.find(key_str);
    if (it == buckets_.end()) {
        // Create new bucket
        TokenBucket bucket(capacity_, refill_rate_);
        bool allowed = bucket.consume(tokens);
        buckets_.emplace(std::move(key_str), std::move(bucket));
        return allowed;
    }

    // Use existing bucket
    return it->second.consume(tokens);
}

void ThreadLocalRateLimiter::reset(std::string_view key) {
    std::string key_str{key};
    auto it = buckets_.find(key_str);
    if (it != buckets_.end()) {
        it->second.reset();
    }
}

void ThreadLocalRateLimiter::clear() {
    buckets_.clear();
}

uint64_t ThreadLocalRateLimiter::available(std::string_view key) const {
    std::string key_str{key};
    auto it = buckets_.find(key_str);
    if (it == buckets_.end()) {
        return capacity_;  // New key would have full capacity
    }
    return it->second.available();
}

} // namespace titan::gateway
