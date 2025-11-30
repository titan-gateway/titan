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

// Titan API Gateway - Main Entry Point
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "control/config.hpp"
#include "core/server_runner.hpp"
#include "core/tls.hpp"

namespace titan::core {
std::atomic<bool> g_server_running{true};
std::atomic<bool> g_graceful_shutdown{false};
// Global upstream manager pointer for metrics (set by worker 0)
std::atomic<const gateway::UpstreamManager*> g_upstream_manager_for_metrics{nullptr};
}  // namespace titan::core

namespace {
// Global ConfigManager for hot-reload support
std::unique_ptr<titan::control::ConfigManager> g_config_manager;
}  // namespace

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        printf("\nReceived shutdown signal (SIGTERM/SIGINT), initiating graceful shutdown...\n");
        titan::core::g_graceful_shutdown = true;
        titan::core::g_server_running = false;
    } else if (signal == SIGHUP) {
        printf("\nReceived SIGHUP signal - reloading configuration...\n");

        if (!g_config_manager) {
            fprintf(stderr, "ERROR: ConfigManager not initialized, cannot reload\n");
            return;
        }

        // Attempt hot-reload using RCU pattern
        bool success = g_config_manager->reload();

        if (success) {
            printf("SUCCESS: Configuration reloaded successfully\n");

            // Print validation warnings if any
            const auto& validation = g_config_manager->last_validation();
            if (!validation.warnings.empty()) {
                printf("Warnings during config reload:\n");
                for (const auto& warning : validation.warnings) {
                    printf("  - %s\n", warning.c_str());
                }
            }
        } else {
            fprintf(stderr, "ERROR: Failed to reload configuration\n");

            // Print validation errors
            const auto& validation = g_config_manager->last_validation();
            if (!validation.errors.empty()) {
                fprintf(stderr, "Configuration validation errors:\n");
                for (const auto& error : validation.errors) {
                    fprintf(stderr, "  - %s\n", error.c_str());
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    printf("Titan API Gateway v0.1.0\n");
    printf("High-performance C++23 API Gateway\n\n");

    // Initialize OpenSSL
    titan::core::initialize_openssl();

    if (argc < 3 || std::string(argv[1]) != "--config") {
        fprintf(stderr, "Usage: %s --config <config.json> [--single-threaded]\n", argv[0]);
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    std::string config_path = argv[2];
    bool single_threaded = false;

    // Check for single-threaded mode flag
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--single-threaded") {
            single_threaded = true;
        }
    }

    // Initialize ConfigManager for hot-reload support
    printf("Loading configuration from %s...\n", config_path.c_str());
    g_config_manager = std::make_unique<titan::control::ConfigManager>();

    if (!g_config_manager->load(config_path)) {
        fprintf(stderr, "Failed to load configuration\n");

        // Print validation errors if available
        const auto& validation = g_config_manager->last_validation();
        if (!validation.errors.empty()) {
            fprintf(stderr, "Configuration validation errors:\n");
            for (const auto& error : validation.errors) {
                fprintf(stderr, "  - %s\n", error.c_str());
            }
        }

        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    // Get initial configuration
    auto config_ptr = g_config_manager->get();
    if (!config_ptr) {
        fprintf(stderr, "Failed to get configuration\n");
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    const titan::control::Config& config = *config_ptr;

    printf("Listening on %s:%u\n", config.server.listen_address.c_str(), config.server.listen_port);

    // Install signal handlers for graceful shutdown and config reload
    std::signal(SIGINT, signal_handler);   // Ctrl+C
    std::signal(SIGTERM, signal_handler);  // Kill signal
    std::signal(SIGHUP, signal_handler);   // Config reload (future)

    // Start server
    std::error_code ec;
    if (single_threaded) {
        printf("Starting Titan in single-threaded mode...\n");
        ec = titan::core::run_simple_server(config);
    } else {
        uint32_t num_workers = config.server.worker_threads;
        if (num_workers == 0) {
            num_workers = titan::core::get_cpu_count();
        }
        printf("Starting Titan with %u worker threads...\n", num_workers);
        ec = titan::core::run_multi_threaded_server(config);
    }

    if (ec) {
        fprintf(stderr, "Server error: %s\n", ec.message().c_str());
        titan::core::cleanup_openssl();
        return EXIT_FAILURE;
    }

    printf("Titan stopped.\n");
    titan::core::cleanup_openssl();
    return EXIT_SUCCESS;
}
