// Titan Unit Tests - Main Entry Point
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../../src/control/config.hpp"
#include "../../src/core/logging.hpp"

// Global test fixture - runs once before all tests
struct GlobalSetup {
    GlobalSetup() {
        // Initialize logging system for tests
        titan::logging::init_logging_system();

        // Use default logging config for tests
        titan::control::LogConfig log_config;
        log_config.output = "/tmp/titan_tests";
        titan::logging::init_worker_logger(0, log_config);
    }

    ~GlobalSetup() {
        // Cleanup logging system
        titan::logging::shutdown_logging();
    }
};

// Create global instance to run setup/teardown
static GlobalSetup g_setup;

TEST_CASE("Basic sanity test", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
