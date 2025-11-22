// Titan Unit Tests - Main Entry Point
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic sanity test", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
