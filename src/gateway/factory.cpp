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

// Gateway Component Factory - Implementation

#include "factory.hpp"

#include "circuit_breaker.hpp"
#include "rate_limit.hpp"

namespace titan::gateway {

std::unique_ptr<Router> build_router(const control::Config& config) {
    auto router = std::make_unique<Router>();

    // Build router from config
    for (const auto& route_config : config.routes) {
        Route route;
        route.path = route_config.path;

        if (!route_config.method.empty()) {
            // Convert method string to enum
            const auto& method_str = route_config.method;
            switch (method_str[0]) {
                case 'G':
                    if (method_str == "GET")
                        route.method = http::Method::GET;
                    break;
                case 'P':
                    if (method_str == "POST")
                        route.method = http::Method::POST;
                    else if (method_str == "PUT")
                        route.method = http::Method::PUT;
                    else if (method_str == "PATCH")
                        route.method = http::Method::PATCH;
                    break;
                case 'D':
                    if (method_str == "DELETE")
                        route.method = http::Method::DELETE;
                    break;
                case 'H':
                    if (method_str == "HEAD")
                        route.method = http::Method::HEAD;
                    break;
                case 'O':
                    if (method_str == "OPTIONS")
                        route.method = http::Method::OPTIONS;
                    break;
            }
        }

        route.handler_id =
            route_config.handler_id.empty() ? route_config.path : route_config.handler_id;
        route.upstream_name = route_config.upstream;
        route.priority = route_config.priority;

        router->add_route(std::move(route));
    }

    return router;
}

std::unique_ptr<UpstreamManager> build_upstream_manager(const control::Config& config) {
    auto upstream_manager = std::make_unique<UpstreamManager>();

    // Build upstreams from config
    for (const auto& upstream_config : config.upstreams) {
        // Calculate pool size as max of all backend max_connections
        size_t pool_size = 64;  // Default
        for (const auto& backend_config : upstream_config.backends) {
            pool_size = std::max(pool_size, static_cast<size_t>(backend_config.max_connections));
        }

        auto upstream = std::make_unique<Upstream>(upstream_config.name, pool_size);

        for (const auto& backend_config : upstream_config.backends) {
            Backend backend;
            backend.host = backend_config.host;
            backend.port = backend_config.port;
            backend.weight = backend_config.weight;
            backend.max_connections = backend_config.max_connections;

            // Initialize circuit breaker if enabled
            if (upstream_config.circuit_breaker.enabled) {
                CircuitBreakerConfig cb_config;
                cb_config.failure_threshold = upstream_config.circuit_breaker.failure_threshold;
                cb_config.success_threshold = upstream_config.circuit_breaker.success_threshold;
                cb_config.timeout_ms = upstream_config.circuit_breaker.timeout_ms;
                cb_config.window_ms = upstream_config.circuit_breaker.window_ms;
                cb_config.enable_global_hints = upstream_config.circuit_breaker.enable_global_hints;
                cb_config.catastrophic_threshold =
                    upstream_config.circuit_breaker.catastrophic_threshold;

                upstream->add_backend_with_circuit_breaker(std::move(backend), cb_config);
            } else {
                upstream->add_backend(std::move(backend));
            }
        }

        // Set load balancing strategy from config
        if (upstream_config.load_balancing == "least_connections") {
            upstream->set_load_balancer(std::make_unique<LeastConnectionsBalancer>());
        } else if (upstream_config.load_balancing == "random") {
            upstream->set_load_balancer(std::make_unique<RandomBalancer>());
        } else if (upstream_config.load_balancing == "weighted_round_robin") {
            upstream->set_load_balancer(std::make_unique<WeightedRoundRobinBalancer>());
        }
        // else: defaults to RoundRobinBalancer (set in Upstream constructor)

        upstream_manager->register_upstream(std::move(upstream));
    }

    return upstream_manager;
}

std::unique_ptr<Pipeline> build_pipeline(const control::Config& config,
                                          UpstreamManager* upstream_manager,
                                          core::RevocationQueue* revocation_queue) {
    auto pipeline = std::make_unique<Pipeline>();

    // Build middleware pipeline (Two-Phase: Request → Response)
    // Order matters: middleware runs in order added
    pipeline->use(std::make_unique<LoggingMiddleware>());
    pipeline->use(std::make_unique<CorsMiddleware>());

    // Rate limiting (only if enabled in config)
    if (config.rate_limit.enabled && config.rate_limit.requests_per_second > 0) {
        RateLimitMiddleware::Config rl_config;
        rl_config.requests_per_second = config.rate_limit.requests_per_second;
        rl_config.burst_size = config.rate_limit.burst_size;
        pipeline->use(std::make_unique<RateLimitMiddleware>(rl_config));
    }

    pipeline->use(std::make_unique<ProxyMiddleware>(upstream_manager));

    return pipeline;
}

}  // namespace titan::gateway
