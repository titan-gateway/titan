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


// Titan Server Runner - Header
// Simple server loop for MVP

#pragma once

#include "server.hpp"
#include "../control/config.hpp"

#include <system_error>

namespace titan::core {

/// Run HTTP server using poll() (single-threaded)
[[nodiscard]] std::error_code run_simple_server(const control::Config& config);

/// Run HTTP server with multi-threading (SO_REUSEPORT + dual epoll)
/// Spawns N worker threads (one per CPU core), each with its own event loop
[[nodiscard]] std::error_code run_multi_threaded_server(const control::Config& config);

} // namespace titan::core
