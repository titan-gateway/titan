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

// Titan Socket Utilities - Implementation

#include "socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "containers.hpp"
#include "logging.hpp"

// Performance instrumentation - enabled by default for profiling
#ifndef TITAN_DISABLE_PERF_INSTRUMENTATION
#define TITAN_ENABLE_FD_TRACKING 1
#endif

namespace titan::core {

#ifdef TITAN_ENABLE_FD_TRACKING
// Thread-local fd tracking for performance analysis
struct FdMetrics {
    std::atomic<uint64_t> close_count{0};
    std::atomic<uint64_t> create_count{0};
    titan::core::fast_map<int, std::string> fd_origins;

    void track_fd(int fd, const char* origin) {
        if (fd >= 0) {
            fd_origins[fd] = origin;
            create_count++;
        }
    }

    void track_close(int fd) {
        close_count.fetch_add(1);
        fd_origins.erase(fd);
    }
};

static thread_local FdMetrics fd_metrics;

// Helper to track fd creation
void track_fd_origin(int fd, const char* origin) {
    fd_metrics.track_fd(fd, origin);
}
#endif

int create_listening_socket(std::string_view address, uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // SO_REUSEADDR - allows binding to same address immediately after restart
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close_fd(fd);
        return -1;
    }

#ifdef SO_REUSEPORT
    // SO_REUSEPORT - allows multiple processes/threads to bind to same port
    // Kernel will load-balance incoming connections across all listening sockets
    // This enables true multi-worker architecture for horizontal CPU scaling
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        close_fd(fd);
        return -1;
    }
#endif

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    std::string addr_str{address};
    if (inet_pton(AF_INET, addr_str.c_str(), &addr.sin_addr) <= 0) {
        close_fd(fd);
        return -1;
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_fd(fd);
        return -1;
    }

    // Listen
    if (listen(fd, backlog) < 0) {
        close_fd(fd);
        return -1;
    }

    // Non-blocking
    if (auto ec = set_nonblocking(fd); ec) {
        close_fd(fd);
        return -1;
    }

#ifdef TITAN_ENABLE_FD_TRACKING
    fd_metrics.track_fd(fd, "listening_socket");
#endif

    return fd;
}

std::error_code set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return std::error_code(errno, std::system_category());
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return std::error_code(errno, std::system_category());
    }

    return {};
}

std::error_code set_reuseaddr(int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return std::error_code(errno, std::system_category());
    }
    return {};
}

void close_fd(int fd) {
    if (fd >= 0) {
#ifdef TITAN_ENABLE_FD_TRACKING
        fd_metrics.track_close(fd);
#endif
        close(fd);  // Actually close the fd (do NOT call close_fd recursively!)
    }
}

}  // namespace titan::core
