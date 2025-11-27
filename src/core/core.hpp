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


// Titan Core Engine - Header
// Thread utilities for CPU affinity and core count

#pragma once

#include <cstdint>
#include <system_error>

namespace titan::core {

/// Pin current thread to a specific CPU core
[[nodiscard]] std::error_code pin_thread_to_core(uint32_t core_id);

/// Get number of available CPU cores
[[nodiscard]] uint32_t get_cpu_count();

} // namespace titan::core
