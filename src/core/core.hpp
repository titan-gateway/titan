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
