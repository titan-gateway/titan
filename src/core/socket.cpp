// Titan Socket Utilities - Implementation

#include "socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace titan::core {

int create_listening_socket(std::string_view address, uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    // SO_REUSEADDR
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(fd);
        return -1;
    }

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

} // namespace titan::core
