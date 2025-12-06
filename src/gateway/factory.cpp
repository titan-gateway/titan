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

#include "../core/jwks_fetcher.hpp"
#include "../core/jwt.hpp"
#include "circuit_breaker.hpp"
#include "jwt_authz_middleware.hpp"
#include "jwt_middleware.hpp"
#include "rate_limit.hpp"

namespace titan::gateway {

std::unique_ptr<Router> build_router(const control::Config& config) {
    auto router = std::make_unique<Router>();

    printf("[DEBUG] build_router: Building router with %zu routes from config\n",
           config.routes.size());

    // Build router from config
    for (const auto& route_config : config.routes) {
        Route route;
        route.path = route_config.path;

        printf(
            "[DEBUG] build_router: Processing route: path=%s, method=%s, handler_id=%s, "
            "upstream=%s, priority=%d, auth_required=%s\n",
            route_config.path.c_str(), route_config.method.c_str(), route_config.handler_id.c_str(),
            route_config.upstream.c_str(), route_config.priority,
            route_config.auth_required ? "true" : "false");

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
        route.auth_required = route_config.auth_required;
        route.required_scopes = route_config.required_scopes;
        route.required_roles = route_config.required_roles;

        printf(
            "[DEBUG] build_router: Adding route to router: path=%s, handler_id=%s, upstream=%s\n",
            route.path.c_str(), route.handler_id.c_str(), route.upstream_name.c_str());
        router->add_route(std::move(route));
    }

    printf("[DEBUG] build_router: Router built successfully with %zu routes\n",
           config.routes.size());
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

std::shared_ptr<core::KeyManager> build_static_key_manager(const control::Config& config) {
    if (!config.jwt.enabled || config.jwt.keys.empty()) {
        return nullptr;
    }

    auto key_manager = std::make_shared<core::KeyManager>();

    for (const auto& key_config : config.jwt.keys) {
        core::JwtAlgorithm alg{};
        if (key_config.algorithm == "RS256") {
            alg = core::JwtAlgorithm::RS256;
        } else if (key_config.algorithm == "ES256") {
            alg = core::JwtAlgorithm::ES256;
        } else if (key_config.algorithm == "HS256") {
            alg = core::JwtAlgorithm::HS256;
        } else {
            continue;  // Skip unknown algorithms
        }

        std::optional<core::VerificationKey> key;
        if (alg == core::JwtAlgorithm::HS256) {
            key = core::VerificationKey::load_hmac_secret(key_config.key_id, key_config.secret);
        } else {
            key = core::VerificationKey::load_public_key(alg, key_config.key_id,
                                                         key_config.public_key_path);
        }

        if (key) {
            key_manager->add_key(std::move(*key));
        }
    }

    return key_manager;
}

std::shared_ptr<core::JwtValidator> build_jwt_validator(
    const control::Config& config, std::shared_ptr<core::KeyManager> static_keys) {
    if (!config.jwt.enabled) {
        return nullptr;
    }

    core::JwtValidatorConfig validator_config;
    validator_config.require_exp = config.jwt.require_exp;
    validator_config.require_sub = config.jwt.require_sub;
    validator_config.allowed_issuers = config.jwt.allowed_issuers;
    validator_config.allowed_audiences = config.jwt.allowed_audiences;
    validator_config.clock_skew_seconds = config.jwt.clock_skew_seconds;
    validator_config.cache_enabled = config.jwt.cache_enabled;
    validator_config.cache_capacity = config.jwt.cache_capacity;

    auto validator = std::make_shared<core::JwtValidator>(validator_config);

    if (static_keys) {
        validator->set_key_manager(static_keys);
    }

    return validator;
}

std::shared_ptr<core::JwksFetcher> build_jwks_fetcher(const control::Config& config) {
    if (!config.jwt.enabled || !config.jwt.jwks.has_value()) {
        return nullptr;
    }

    const auto& jwks_schema = config.jwt.jwks.value();

    // Convert JwksConfigSchema to JwksConfig
    core::JwksConfig fetcher_config;
    fetcher_config.url = jwks_schema.url;
    fetcher_config.refresh_interval_seconds = jwks_schema.refresh_interval_seconds;
    fetcher_config.timeout_seconds = jwks_schema.timeout_seconds;
    fetcher_config.retry_max = jwks_schema.retry_max;
    fetcher_config.circuit_breaker_seconds = jwks_schema.circuit_breaker_seconds;

    auto fetcher = std::make_shared<core::JwksFetcher>(fetcher_config);

    return fetcher;
}

std::unique_ptr<Pipeline> build_pipeline(const control::Config& config,
                                         UpstreamManager* upstream_manager,
                                         core::RevocationQueue* revocation_queue,
                                         std::shared_ptr<core::JwtValidator> jwt_validator) {
    auto pipeline = std::make_unique<Pipeline>();

    // Build middleware pipeline (Two-Phase: Request → Response)
    // Order matters: middleware runs in order added
    pipeline->use(std::make_unique<LoggingMiddleware>());
    pipeline->use(std::make_unique<CorsMiddleware>());

    // JWT authentication (only if enabled and validator provided)
    if (config.jwt.enabled && jwt_validator) {
        JwtAuthMiddleware::Config jwt_config;
        jwt_config.header = config.jwt.header;
        jwt_config.scheme = config.jwt.scheme;
        jwt_config.enabled = true;
        jwt_config.revocation_enabled = config.jwt.revocation_enabled;

        pipeline->use(
            std::make_unique<JwtAuthMiddleware>(jwt_config, jwt_validator, revocation_queue));
    }

    // JWT authorization (claims-based access control)
    // Must run AFTER JwtAuthMiddleware to have access to validated claims
    if (config.jwt.enabled && config.jwt_authz.enabled) {
        JwtAuthzMiddleware::Config authz_config;
        authz_config.enabled = config.jwt_authz.enabled;
        authz_config.scope_claim = config.jwt_authz.scope_claim;
        authz_config.roles_claim = config.jwt_authz.roles_claim;
        authz_config.require_all_scopes = config.jwt_authz.require_all_scopes;
        authz_config.require_all_roles = config.jwt_authz.require_all_roles;

        pipeline->use(std::make_unique<JwtAuthzMiddleware>(authz_config));
    }

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
