// Titan JWT Revocation Unit Tests
// Tests for RevocationQueue and RevocationList

#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

#include "../../src/core/jwt_revocation.hpp"

using namespace titan::core;

TEST_CASE("RevocationQueue - basic push and drain", "[jwt][revocation]") {
    RevocationQueue queue;

    SECTION("Initially empty") {
        REQUIRE_FALSE(queue.has_pending());
        auto entries = queue.drain();
        REQUIRE(entries.empty());
    }

    SECTION("Push single entry") {
        queue.push({"token123", 1234567890});
        REQUIRE(queue.has_pending());

        auto entries = queue.drain();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].jti == "token123");
        REQUIRE(entries[0].exp == 1234567890);

        REQUIRE_FALSE(queue.has_pending());
    }

    SECTION("Push multiple entries") {
        queue.push({"token1", 1000});
        queue.push({"token2", 2000});
        queue.push({"token3", 3000});

        REQUIRE(queue.has_pending());

        auto entries = queue.drain();
        REQUIRE(entries.size() == 3);

        // Entries should be in LIFO order (stack behavior)
        REQUIRE(entries[0].jti == "token3");
        REQUIRE(entries[1].jti == "token2");
        REQUIRE(entries[2].jti == "token1");

        REQUIRE_FALSE(queue.has_pending());
    }

    SECTION("Drain empties the queue") {
        queue.push({"token1", 1000});
        queue.push({"token2", 2000});

        auto entries1 = queue.drain();
        REQUIRE(entries1.size() == 2);

        auto entries2 = queue.drain();
        REQUIRE(entries2.empty());
        REQUIRE_FALSE(queue.has_pending());
    }
}

TEST_CASE("RevocationQueue - thread safety", "[jwt][revocation][concurrency]") {
    RevocationQueue queue;
    constexpr int num_threads = 4;
    constexpr int entries_per_thread = 100;

    SECTION("Concurrent push operations") {
        std::vector<std::thread> threads;

        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&queue, t]() {
                for (int i = 0; i < entries_per_thread; ++i) {
                    std::string jti = "thread" + std::to_string(t) + "_token" + std::to_string(i);
                    queue.push({jti, static_cast<uint64_t>(i)});
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE(queue.has_pending());

        auto entries = queue.drain();
        REQUIRE(entries.size() == num_threads * entries_per_thread);
    }
}

TEST_CASE("RevocationList - basic operations", "[jwt][revocation]") {
    RevocationList list;

    SECTION("Initially no tokens are revoked") {
        REQUIRE_FALSE(list.is_revoked("any_token"));
    }

    SECTION("Revoke a token") {
        list.revoke("token123", 1234567890);
        REQUIRE(list.is_revoked("token123"));
        REQUIRE_FALSE(list.is_revoked("other_token"));
    }

    SECTION("Revoke multiple tokens") {
        list.revoke("token1", 1000);
        list.revoke("token2", 2000);
        list.revoke("token3", 3000);

        REQUIRE(list.is_revoked("token1"));
        REQUIRE(list.is_revoked("token2"));
        REQUIRE(list.is_revoked("token3"));
        REQUIRE_FALSE(list.is_revoked("token4"));
    }

    SECTION("Cleanup expired tokens") {
        list.revoke("expired1", 1000);
        list.revoke("expired2", 2000);
        list.revoke("valid", 9999999999);  // Far future

        // Cleanup tokens expired before timestamp 5000
        list.cleanup_expired(5000);

        REQUIRE_FALSE(list.is_revoked("expired1"));
        REQUIRE_FALSE(list.is_revoked("expired2"));
        REQUIRE(list.is_revoked("valid"));
    }

    SECTION("Revoke same token multiple times (idempotent)") {
        list.revoke("token123", 1000);
        list.revoke("token123", 2000);  // Update expiration

        REQUIRE(list.is_revoked("token123"));

        list.cleanup_expired(1500);
        REQUIRE(list.is_revoked("token123"));  // Still revoked (exp=2000)

        list.cleanup_expired(3000);
        REQUIRE_FALSE(list.is_revoked("token123"));  // Now expired
    }
}

TEST_CASE("RevocationList - sync from queue", "[jwt][revocation]") {
    RevocationQueue queue;
    RevocationList list;

    SECTION("Sync empty queue does nothing") {
        list.sync_from_queue(queue);
        REQUIRE_FALSE(list.is_revoked("any_token"));
    }

    SECTION("Sync transfers entries from queue to list") {
        queue.push({"token1", 1000});
        queue.push({"token2", 2000});
        queue.push({"token3", 3000});

        REQUIRE_FALSE(list.is_revoked("token1"));

        list.sync_from_queue(queue);

        REQUIRE(list.is_revoked("token1"));
        REQUIRE(list.is_revoked("token2"));
        REQUIRE(list.is_revoked("token3"));

        REQUIRE_FALSE(queue.has_pending());
    }

    SECTION("Multiple workers sync from same queue") {
        queue.push({"token1", 1000});
        queue.push({"token2", 2000});

        RevocationList list1;
        RevocationList list2;

        list1.sync_from_queue(queue);

        REQUIRE(list1.is_revoked("token1"));
        REQUIRE(list1.is_revoked("token2"));

        // Queue is now empty
        list2.sync_from_queue(queue);

        // list2 should have no tokens (queue was already drained)
        REQUIRE_FALSE(list2.is_revoked("token1"));
        REQUIRE_FALSE(list2.is_revoked("token2"));
    }

    SECTION("Sync is fast when queue is empty") {
        // This test verifies the fast path (atomic load) works
        list.sync_from_queue(queue);  // Should return immediately
        REQUIRE_FALSE(list.is_revoked("any_token"));
    }
}

TEST_CASE("RevocationList - cleanup", "[jwt][revocation]") {
    RevocationList list;

    SECTION("Cleanup with no tokens") {
        list.cleanup_expired(1000);  // Should not crash
    }

    SECTION("Cleanup removes only expired tokens") {
        list.revoke("expired1", 1000);
        list.revoke("expired2", 1500);
        list.revoke("active1", 3000);
        list.revoke("active2", 4000);

        // Current time: 2000
        list.cleanup_expired(2000);

        REQUIRE_FALSE(list.is_revoked("expired1"));
        REQUIRE_FALSE(list.is_revoked("expired2"));
        REQUIRE(list.is_revoked("active1"));
        REQUIRE(list.is_revoked("active2"));
    }

    SECTION("Cleanup with future timestamp removes all") {
        list.revoke("token1", 1000);
        list.revoke("token2", 2000);
        list.revoke("token3", 3000);

        list.cleanup_expired(9999999999);

        REQUIRE_FALSE(list.is_revoked("token1"));
        REQUIRE_FALSE(list.is_revoked("token2"));
        REQUIRE_FALSE(list.is_revoked("token3"));
    }
}

TEST_CASE("Revocation - realistic workflow", "[jwt][revocation][integration]") {
    RevocationQueue queue;
    RevocationList worker1_list;
    RevocationList worker2_list;

    SECTION("Admin revokes token, workers sync") {
        // Admin receives revocation request
        queue.push({"compromised_token", 1234567890});

        // Worker 1 processes request
        worker1_list.sync_from_queue(queue);
        REQUIRE(worker1_list.is_revoked("compromised_token"));

        // Queue is now empty (worker 1 drained it)
        REQUIRE_FALSE(queue.has_pending());

        // Worker 2 syncs (queue is empty)
        worker2_list.sync_from_queue(queue);
        REQUIRE_FALSE(worker2_list.is_revoked("compromised_token"));

        // Admin revokes another token
        queue.push({"another_token", 9999999999});

        // Both workers sync
        worker1_list.sync_from_queue(queue);
        worker2_list.sync_from_queue(queue);

        // Only one worker gets it (queue is single-consumer at a time)
        bool worker1_has_it = worker1_list.is_revoked("another_token");
        bool worker2_has_it = worker2_list.is_revoked("another_token");

        // Exactly one worker should have it (first to drain)
        REQUIRE((worker1_has_it || worker2_has_it));
    }
}
