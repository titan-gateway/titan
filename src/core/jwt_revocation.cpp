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

#include "jwt_revocation.hpp"

#include <algorithm>

namespace titan::core {

// ============================================================================
// RevocationQueue - Lock-Free Queue Implementation
// ============================================================================

RevocationQueue::RevocationQueue() = default;

RevocationQueue::~RevocationQueue() {
    // Clean up all nodes
    Node* current = head_.load(std::memory_order_relaxed);
    while (current) {
        Node* next = current->next.load(std::memory_order_relaxed);
        delete current;
        current = next;
    }
}

RevocationQueue::RevocationQueue(RevocationQueue&& other) noexcept
    : head_(other.head_.exchange(nullptr, std::memory_order_acquire)),
      size_(other.size_.exchange(0, std::memory_order_relaxed)) {}

RevocationQueue& RevocationQueue::operator=(RevocationQueue&& other) noexcept {
    if (this != &other) {
        // Clean up existing nodes
        Node* current = head_.load(std::memory_order_relaxed);
        while (current) {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }

        head_.store(other.head_.exchange(nullptr, std::memory_order_acquire),
                    std::memory_order_release);
        size_.store(other.size_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void RevocationQueue::push(RevocationEntry entry) {
    // Create new node
    auto* new_node = new Node(std::move(entry));

    // Push to head (LIFO stack)
    // This is the classic lock-free stack push algorithm
    Node* old_head = head_.load(std::memory_order_relaxed);
    do {
        new_node->next.store(old_head, std::memory_order_relaxed);
    } while (!head_.compare_exchange_weak(old_head, new_node, std::memory_order_release,
                                          std::memory_order_relaxed));

    // Increment size (approximate, for has_pending())
    size_.fetch_add(1, std::memory_order_relaxed);
}

std::vector<RevocationEntry> RevocationQueue::drain() {
    // Atomically swap head with nullptr (take ownership of entire queue)
    Node* head = head_.exchange(nullptr, std::memory_order_acquire);

    if (!head) {
        return {};  // Queue was empty
    }

    // Collect all entries
    std::vector<RevocationEntry> entries;
    Node* current = head;
    while (current) {
        entries.push_back(std::move(current->entry));
        Node* next = current->next.load(std::memory_order_relaxed);
        delete current;
        current = next;
    }

    // Reset size counter
    size_.store(0, std::memory_order_relaxed);

    return entries;
}

bool RevocationQueue::has_pending() const noexcept {
    return size_.load(std::memory_order_relaxed) > 0;
}

// ============================================================================
// RevocationList - Thread-Local Blacklist Implementation
// ============================================================================

void RevocationList::revoke(std::string_view jti, uint64_t exp) {
    // Insert or update expiration time
    blacklist_[std::string(jti)] = exp;
}

bool RevocationList::is_revoked(std::string_view jti) const noexcept {
    // O(1) hash lookup
    return blacklist_.find(std::string(jti)) != blacklist_.end();
}

void RevocationList::sync_from_queue(RevocationQueue& queue) {
    // Fast path: check if queue has pending entries
    if (!queue.has_pending()) {
        return;  // No revocations, skip drain
    }

    // Drain all pending revocations from global queue
    auto entries = queue.drain();

    // Add to local blacklist
    for (auto& entry : entries) {
        revoke(entry.jti, entry.exp);
    }
}

void RevocationList::cleanup_expired(uint64_t current_time_seconds) {
    // Remove entries where exp < current_time
    // Use erase_if (C++20) for efficient removal
    std::erase_if(blacklist_, [current_time_seconds](const auto& pair) {
        return pair.second < current_time_seconds;  // exp < now
    });
}

}  // namespace titan::core
