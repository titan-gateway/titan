// Titan Server Runner - Header
// Simple server loop for MVP (Phase 5)

#pragma once

#include "server.hpp"
#include "../control/config.hpp"

#include <system_error>

namespace titan::core {

/// Run HTTP server using poll() (single-threaded)
[[nodiscard]] std::error_code run_simple_server(const control::Config& config);

} // namespace titan::core
