// Titan Core Engine Unit Tests

#include "../../src/core/core.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace titan::core;

TEST_CASE("CPU count detection", "[core][cpu]") {
    uint32_t count = get_cpu_count();
    REQUIRE(count > 0);
    REQUIRE(count <= 256); // Reasonable upper bound
}

TEST_CASE("Thread pinning", "[core][affinity]") {
    // Pin to core 0
    auto ec = pin_thread_to_core(0);

#ifdef __linux__
    // Should succeed on Linux
    REQUIRE_FALSE(ec);
#else
    // May be no-op on other platforms
    (void)ec;
#endif
}
