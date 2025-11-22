// Titan API Gateway - Main Entry Point
#include "core/server_runner.hpp"
#include "core/tls.hpp"
#include "control/config.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace titan::core {
    std::atomic<bool> g_server_running{true};
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        printf("\nReceived shutdown signal, stopping Titan...\n");
        titan::core::g_server_running = false;
    }
}

int main(int argc, char* argv[]) {
    printf("Titan API Gateway v0.1.0\n");
    printf("High-performance C++23 API Gateway\n\n");

    // Initialize OpenSSL
    titan::core::initialize_openssl();

    if (argc < 3 || std::string(argv[1]) != "--config") {
        fprintf(stderr, "Usage: %s --config <config.json>\n", argv[0]);
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    std::string config_path = argv[2];

    // Load configuration
    printf("Loading configuration from %s...\n", config_path.c_str());
    auto maybe_config = titan::control::ConfigLoader::load_from_file(config_path);
    if (!maybe_config.has_value()) {
        fprintf(stderr, "Failed to load configuration\n");
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    titan::control::Config config = std::move(*maybe_config);

    printf("Listening on %s:%u\n",
           config.server.listen_address.c_str(),
           config.server.listen_port);

    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start server
    printf("Starting Titan...\n");
    std::error_code ec = titan::core::run_simple_server(config);

    if (ec) {
        fprintf(stderr, "Server error: %s\n", ec.message().c_str());
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    printf("Titan stopped.\n");
    titan::core::cleanup_openssl();
    return EXIT_SUCCESS;
}
