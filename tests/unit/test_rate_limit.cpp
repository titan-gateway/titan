// Rate Limiting Tests

#include "gateway/rate_limit.hpp"
#include "gateway/pipeline.hpp"
#include "http/http.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace titan::gateway;
using namespace titan::http;

TEST_CASE("TokenBucket basic operations", "[gateway][rate_limit]") {
    SECTION("Initial state") {
        TokenBucket bucket(100, 10);  // 100 tokens capacity, 10 tokens/sec

        REQUIRE(bucket.capacity() == 100);
        REQUIRE(bucket.refill_rate() == 10);
        REQUIRE(bucket.available() == 100);  // Starts full
    }

    SECTION("Consume tokens") {
        TokenBucket bucket(100, 10);

        REQUIRE(bucket.consume(10));  // Consume 10 tokens
        REQUIRE(bucket.available() == 90);

        REQUIRE(bucket.consume(50));  // Consume 50 more
        REQUIRE(bucket.available() == 40);
    }

    SECTION("Insufficient tokens") {
        TokenBucket bucket(10, 1);

        REQUIRE(bucket.consume(5));   // 10 -> 5
        REQUIRE(bucket.consume(3));   // 5 -> 2
        REQUIRE_FALSE(bucket.consume(5));  // Not enough tokens (need 5, have 2)
        REQUIRE(bucket.available() == 2);  // Still 2 (consumption failed)
    }

    SECTION("Exact consumption") {
        TokenBucket bucket(10, 1);

        REQUIRE(bucket.consume(10));  // Consume all tokens
        REQUIRE(bucket.available() == 0);
        REQUIRE_FALSE(bucket.consume(1));  // No tokens left
    }

    SECTION("Reset bucket") {
        TokenBucket bucket(100, 10);

        bucket.consume(80);
        REQUIRE(bucket.available() == 20);

        bucket.reset();
        REQUIRE(bucket.available() == 100);  // Back to full capacity
    }
}

TEST_CASE("TokenBucket refill over time", "[gateway][rate_limit]") {
    SECTION("Refill after delay") {
        TokenBucket bucket(100, 100);  // 100 tokens/sec = 1 token per 10ms

        bucket.consume(50);  // 100 -> 50
        REQUIRE(bucket.available() == 50);

        // Sleep for 100ms (should refill ~10 tokens)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Trigger refill by trying to consume
        bucket.consume(0);  // No-op consume to trigger refill
        REQUIRE(bucket.available() >= 60);  // At least 10 tokens refilled
        REQUIRE(bucket.available() <= 100);  // Capped at capacity
    }

    SECTION("Refill caps at capacity") {
        TokenBucket bucket(10, 100);  // Small capacity

        bucket.consume(5);  // 10 -> 5

        // Sleep longer than needed to refill
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        bucket.consume(0);  // Trigger refill
        REQUIRE(bucket.available() == 10);  // Capped at capacity, not more
    }
}

TEST_CASE("ThreadLocalRateLimiter basic operations", "[gateway][rate_limit]") {
    SECTION("Single key") {
        ThreadLocalRateLimiter limiter(100, 10);  // 100 burst, 10/sec

        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.key_count() == 1);
    }

    SECTION("Multiple keys") {
        ThreadLocalRateLimiter limiter(10, 1);

        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client2"));
        REQUIRE(limiter.allow("client3"));

        REQUIRE(limiter.key_count() == 3);
    }

    SECTION("Rate limit enforcement per key") {
        ThreadLocalRateLimiter limiter(5, 0);  // 5 tokens, no refill

        // Client1 - consume all 5 tokens
        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client1"));
        REQUIRE(limiter.allow("client1"));
        REQUIRE_FALSE(limiter.allow("client1"));  // 6th request denied

        // Client2 - still has full capacity
        REQUIRE(limiter.allow("client2"));
        REQUIRE(limiter.available("client2") == 4);
    }

    SECTION("Available tokens") {
        ThreadLocalRateLimiter limiter(100, 10);

        REQUIRE(limiter.available("new_client") == 100);  // New client has full capacity

        limiter.allow("client1", 30);  // Consume 30 tokens
        REQUIRE(limiter.available("client1") == 70);
    }

    SECTION("Reset key") {
        ThreadLocalRateLimiter limiter(10, 0);

        // Exhaust tokens
        for (int i = 0; i < 10; ++i) {
            limiter.allow("client1");
        }
        REQUIRE_FALSE(limiter.allow("client1"));

        // Reset
        limiter.reset("client1");
        REQUIRE(limiter.allow("client1"));  // Can now make requests again
    }

    SECTION("Clear all keys") {
        ThreadLocalRateLimiter limiter(10, 1);

        limiter.allow("client1");
        limiter.allow("client2");
        limiter.allow("client3");
        REQUIRE(limiter.key_count() == 3);

        limiter.clear();
        REQUIRE(limiter.key_count() == 0);
    }
}

TEST_CASE("ThreadLocalRateLimiter multi-token consumption", "[gateway][rate_limit]") {
    SECTION("Consume multiple tokens at once") {
        ThreadLocalRateLimiter limiter(100, 10);

        REQUIRE(limiter.allow("client1", 50));  // Consume 50 tokens
        REQUIRE(limiter.available("client1") == 50);

        REQUIRE(limiter.allow("client1", 30));  // Consume 30 more
        REQUIRE(limiter.available("client1") == 20);

        REQUIRE_FALSE(limiter.allow("client1", 30));  // Not enough (need 30, have 20)
        REQUIRE(limiter.available("client1") == 20);  // Still 20
    }
}

TEST_CASE("RateLimitMiddleware integration", "[gateway][rate_limit][middleware]") {
    SECTION("Default configuration") {
        RateLimitMiddleware middleware;

        Request req;
        Response res;
        RouteMatch match;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.route_match = match;
        ctx.client_ip = "192.168.1.1";

        // First request should succeed
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }

    SECTION("Custom configuration") {
        RateLimitMiddleware::Config config;
        config.requests_per_second = 10;
        config.burst_size = 20;

        RateLimitMiddleware middleware(config);

        Request req;
        Response res;
        RouteMatch match;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.route_match = match;
        ctx.client_ip = "192.168.1.1";

        // Should allow first request
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
    }

    SECTION("Rate limit exceeded") {
        RateLimitMiddleware::Config config;
        config.requests_per_second = 0;  // No refill
        config.burst_size = 3;           // Only 3 requests allowed

        RateLimitMiddleware middleware(config);

        Request req;
        Response res;
        RouteMatch match;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.route_match = match;
        ctx.client_ip = "192.168.1.1";

        // First 3 requests succeed
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);

        // 4th request should be rate limited
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Stop);
        REQUIRE(res.status == StatusCode::TooManyRequests);
        REQUIRE(ctx.has_error);
    }

    SECTION("Different IPs tracked separately") {
        RateLimitMiddleware::Config config;
        config.requests_per_second = 0;
        config.burst_size = 2;

        RateLimitMiddleware middleware(config);

        Request req;
        Response res;
        RouteMatch match;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.route_match = match;

        // Client 1 - exhaust limit
        ctx.client_ip = "192.168.1.1";
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Stop);  // Rate limited

        // Client 2 - still has capacity
        ctx.client_ip = "192.168.1.2";
        ctx.has_error = false;  // Clear previous error
        ctx.error_message.clear();
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
        REQUIRE(middleware.process_request(ctx) == MiddlewareResult::Continue);
    }

    SECTION("Missing client IP") {
        RateLimitMiddleware middleware;

        Request req;
        Response res;
        RouteMatch match;

        RequestContext ctx;
        ctx.request = &req;
        ctx.response = &res;
        ctx.route_match = match;
        ctx.client_ip = "";  // No client IP

        // Should allow request (or could deny, depending on policy)
        auto result = middleware.process_request(ctx);
        REQUIRE(result == MiddlewareResult::Continue);
    }
}

TEST_CASE("Rate limiting realistic scenario", "[gateway][rate_limit]") {
    // Simulate 100 req/s with 200 burst
    RateLimitMiddleware::Config config;
    config.requests_per_second = 100;
    config.burst_size = 200;

    RateLimitMiddleware middleware(config);

    Request req;
    Response res;
    RouteMatch match;

    RequestContext ctx;
    ctx.request = &req;
    ctx.response = &res;
    ctx.route_match = match;
    ctx.client_ip = "10.0.0.1";

    SECTION("Burst within limit") {
        // Send 150 requests rapidly (within burst limit)
        int allowed = 0;
        for (int i = 0; i < 150; ++i) {
            if (middleware.process_request(ctx) == MiddlewareResult::Continue) {
                allowed++;
            }
            ctx.has_error = false;  // Clear error for next iteration
            ctx.error_message.clear();
        }

        REQUIRE(allowed == 150);  // All should be allowed
    }

    SECTION("Burst exceeds limit") {
        // Send 250 requests rapidly (exceeds burst limit of 200)
        int allowed = 0;
        for (int i = 0; i < 250; ++i) {
            if (middleware.process_request(ctx) == MiddlewareResult::Continue) {
                allowed++;
            }
            ctx.has_error = false;
            ctx.error_message.clear();
        }

        REQUIRE(allowed == 200);  // Only burst_size allowed
    }
}
