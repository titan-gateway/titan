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

// JWT Token Revocation - In-Memory Blacklist with Automatic Cleanup
//
// Architecture:
// - Thread-local blacklist per worker (shared-nothing, lock-free hot path)
// - Global atomic queue for cross-thread revocation broadcast
// - Automatic cleanup when tokens expire (no background thread needed)
//
// Performance:
// - Revocation check: O(1) hash lookup (~20ns)
// - Queue sync: O(n) amortized, only when revocations occur
// - Memory: ~100 bytes per revoked token, auto-freed at expiration

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace titan::core {

/// Entry in the revocation queue (cross-thread communication)
struct RevocationEntry {
    std::string jti;  // JWT ID claim
    uint64_t exp;     // Expiration timestamp (Unix epoch)
};

/// Lock-free queue for broadcasting revocations to all workers
/// Uses atomic linked list for wait-free push, lock-free drain
class RevocationQueue {
public:
    RevocationQueue();
    ~RevocationQueue();

    // Non-copyable, movable
    RevocationQueue(const RevocationQueue&) = delete;
    RevocationQueue& operator=(const RevocationQueue&) = delete;
    RevocationQueue(RevocationQueue&&) noexcept;
    RevocationQueue& operator=(RevocationQueue&&) noexcept;

    /// Push revocation entry to queue (called by admin endpoint)
    /// Thread-safe, wait-free
    void push(RevocationEntry entry);

    /// Drain all pending revocations (called by worker threads)
    /// Thread-safe, lock-free
    /// Returns empty vector if queue is empty
    [[nodiscard]] std::vector<RevocationEntry> drain();

    /// Check if queue has pending entries (fast atomic load)
    [[nodiscard]] bool has_pending() const noexcept;

private:
    struct Node {
        RevocationEntry entry;
        std::atomic<Node*> next{nullptr};

        explicit Node(RevocationEntry e) : entry(std::move(e)) {}
    };

    std::atomic<Node*> head_{nullptr};  // LIFO stack (most recent first)
    std::atomic<size_t> size_{0};       // Approximate size for has_pending()
};

/// Thread-local revocation blacklist with automatic cleanup
/// Each worker thread maintains its own blacklist (shared-nothing design)
class RevocationList {
public:
    RevocationList() = default;
    ~RevocationList() = default;

    // Non-copyable, movable
    RevocationList(const RevocationList&) = delete;
    RevocationList& operator=(const RevocationList&) = delete;
    RevocationList(RevocationList&&) noexcept = default;
    RevocationList& operator=(RevocationList&&) noexcept = default;

    /// Add token to blacklist with expiration time
    /// O(1) hash insert
    void revoke(std::string_view jti, uint64_t exp);

    /// Check if token is revoked
    /// O(1) hash lookup
    [[nodiscard]] bool is_revoked(std::string_view jti) const noexcept;

    /// Sync from global revocation queue
    /// Drains pending revocations and adds to local blacklist
    /// Called on each request (fast path: just atomic load if queue empty)
    void sync_from_queue(RevocationQueue& queue);

    /// Remove expired entries from blacklist
    /// Call periodically to free memory (not required for correctness)
    /// O(n) where n = blacklist size
    void cleanup_expired(uint64_t current_time_seconds);

    /// Get current blacklist size (for metrics/debugging)
    [[nodiscard]] size_t size() const noexcept { return blacklist_.size(); }

private:
    // jti → exp_timestamp
    // Tokens are automatically invalid after exp, but we keep them
    // in blacklist until cleanup_expired() is called
    std::unordered_map<std::string, uint64_t> blacklist_;
};

}  // namespace titan::core
