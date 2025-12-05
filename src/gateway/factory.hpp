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

// Gateway Component Factory - Header
// Factory functions for building gateway components (Router, Upstreams, Pipeline)

#pragma once

#include <memory>

#include "../control/config.hpp"
#include "../core/jwt.hpp"
#include "../core/jwt_revocation.hpp"
#include "../core/jwks_fetcher.hpp"
#include "pipeline.hpp"
#include "router.hpp"
#include "upstream.hpp"

namespace titan::gateway {

/// Build router from configuration
[[nodiscard]] std::unique_ptr<Router> build_router(const control::Config& config);

/// Build upstream manager from configuration
[[nodiscard]] std::unique_ptr<UpstreamManager> build_upstream_manager(const control::Config& config);

/// Build static JWT key manager from configuration (shared across workers)
[[nodiscard]] std::shared_ptr<core::KeyManager> build_static_key_manager(
    const control::Config& config);

/// Build JWT validator (per-worker, has thread-local cache)
[[nodiscard]] std::shared_ptr<core::JwtValidator> build_jwt_validator(
    const control::Config& config, std::shared_ptr<core::KeyManager> static_keys = nullptr);

/// Build JWKS fetcher if enabled (per-worker, has background thread)
[[nodiscard]] std::shared_ptr<core::JwksFetcher> build_jwks_fetcher(
    const control::Config& config);

/// Build middleware pipeline from configuration
[[nodiscard]] std::unique_ptr<Pipeline> build_pipeline(
    const control::Config& config, UpstreamManager* upstream_manager,
    core::RevocationQueue* revocation_queue,
    std::shared_ptr<core::JwtValidator> jwt_validator = nullptr);

}  // namespace titan::gateway
