// Titan Socket Utilities - Header

#pragma once

#include <cstdint>
#include <string_view>
#include <system_error>

namespace titan::core {

/// Create non-blocking listening socket
[[nodiscard]] int create_listening_socket(
    std::string_view address,
    uint16_t port,
    int backlog = 128);

[[nodiscard]] std::error_code set_nonblocking(int fd);
[[nodiscard]] std::error_code set_reuseaddr(int fd);

void close_fd(int fd);

} // namespace titan::core
