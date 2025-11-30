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

#include <cstdio>
#include <cstring>

namespace titan::core {

int create_listening_socket(std::string_view address, uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // SO_REUSEADDR - allows binding to same address immediately after restart
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(fd);
        return -1;
    }

#ifdef SO_REUSEPORT
    // SO_REUSEPORT - allows multiple processes/threads to bind to same port
    // Kernel will load-balance incoming connections across all listening sockets
    // This enables true multi-worker architecture for horizontal CPU scaling
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        close(fd);
        return -1;
    }
#endif

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    std::string addr_str{address};
    if (inet_pton(AF_INET, addr_str.c_str(), &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // Listen
    if (listen(fd, backlog) < 0) {
        close(fd);
        return -1;
    }

    // Non-blocking
    if (auto ec = set_nonblocking(fd); ec) {
        close(fd);
        return -1;
    }

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
        close(fd);
    }
}

}  // namespace titan::core
