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

// Backend Proxy Service - Implementation
// Manages backend connections and async I/O operations

#include "backend_proxy_service.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <cerrno>

#include "logging.hpp"
#include "socket.hpp"

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#endif

namespace titan::core {

BackendProxyService::BackendProxyService() {
    // Create backend epoll/kqueue instance for non-blocking backend I/O
#ifdef __linux__
    backend_epoll_fd_ = epoll_create1(0);
    if (backend_epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create backend epoll instance");
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    backend_epoll_fd_ = kqueue();
    if (backend_epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create backend kqueue instance");
    }
#endif
}

BackendProxyService::~BackendProxyService() {
    if (backend_epoll_fd_ >= 0) {
        close(backend_epoll_fd_);
    }
}

int BackendProxyService::connect_to_backend(const std::string& host, uint16_t port) {
    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Resolve hostname
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try direct IP first (fastest path)
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Check DNS cache before doing expensive resolution
        auto cache_it = dns_cache_.find(host);
        if (cache_it != dns_cache_.end()) {
            // Cache hit - reuse resolved address
            addr = cache_it->second;
            addr.sin_port = htons(port);  // Port might differ
        } else {
            // Cache miss - perform DNS resolution
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
                close_fd(sockfd);
                return -1;
            }

            addr = *reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            addr.sin_port = htons(port);
            freeaddrinfo(result);

            // Store in cache for future connections
            dns_cache_[host] = addr;
        }
    }

    // Connect (blocking for MVP - TODO: non-blocking + io_uring)
    if (connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_fd(sockfd);
        return -1;
    }

    // Enable TCP_NODELAY to reduce latency (disable Nagle's algorithm)
    // This is critical for API gateway workloads with small messages
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    return sockfd;
}

int BackendProxyService::connect_to_backend_async(const std::string& host, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Make socket non-blocking BEFORE connect
    if (auto ec = set_nonblocking(sockfd); ec) {
        close_fd(sockfd);
        return -1;
    }

    // Enable TCP_NODELAY immediately
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

    // Resolve address (same as blocking version)
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try direct IP first
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Check DNS cache
        auto cache_it = dns_cache_.find(host);
        if (cache_it != dns_cache_.end()) {
            addr = cache_it->second;
            addr.sin_port = htons(port);
        } else {
            // DNS resolution (still blocking for now - TODO: async DNS)
            struct addrinfo hints{}, *result = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
                close_fd(sockfd);
                return -1;
            }

            addr = *reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
            addr.sin_port = htons(port);
            freeaddrinfo(result);

            dns_cache_[host] = addr;
        }
    }

    // Non-blocking connect - will return EINPROGRESS
    int result = connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    if (result < 0) {
        // EINPROGRESS is expected for non-blocking connect
        if (errno != EINPROGRESS) {
            close_fd(sockfd);
            return -1;
        }
        // Connection in progress - epoll will notify when ready
    }

    return sockfd;
}

bool BackendProxyService::add_backend_to_epoll(int backend_fd, uint32_t events) {
#ifdef __linux__
    struct epoll_event ev {};
    ev.events = events;
    ev.data.fd = backend_fd;

    if (epoll_ctl(backend_epoll_fd_, EPOLL_CTL_ADD, backend_fd, &ev) < 0) {
        LOG_ERROR(logger_, "Failed to add backend_fd={} to epoll: {}", backend_fd,
                  std::strerror(errno));
        return false;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent kev[2];
    int n = 0;

    if (events & EPOLLIN) {
        EV_SET(&kev[n++], backend_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & EPOLLOUT) {
        EV_SET(&kev[n++], backend_fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (kevent(backend_epoll_fd_, kev, n, nullptr, 0, nullptr) < 0) {
        LOG_ERROR(logger_, "Failed to add backend_fd={} to kqueue: {}", backend_fd,
                  std::strerror(errno));
        return false;
    }
#endif

    return true;
}

bool BackendProxyService::remove_backend_from_epoll(int backend_fd) {
#ifdef __linux__
    if (epoll_ctl(backend_epoll_fd_, EPOLL_CTL_DEL, backend_fd, nullptr) < 0) {
        // Not necessarily an error - socket might already be closed
        LOG_DEBUG(logger_, "Could not remove backend_fd={} from epoll (might be closed): {}",
                  backend_fd, std::strerror(errno));
        return false;
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    struct kevent kev[2];
    EV_SET(&kev[0], backend_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&kev[1], backend_fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    if (kevent(backend_epoll_fd_, kev, 2, nullptr, 0, nullptr) < 0) {
        LOG_DEBUG(logger_, "Could not remove backend_fd={} from kqueue (might be closed): {}",
                  backend_fd, std::strerror(errno));
        return false;
    }
#endif

    return true;
}

void BackendProxyService::register_backend_connection(int backend_fd, int client_fd,
                                                       int32_t stream_id) {
    backend_connections_[backend_fd] = {client_fd, stream_id};
}

void BackendProxyService::unregister_backend_connection(int backend_fd) {
    backend_connections_.erase(backend_fd);
}

std::pair<int, int32_t> BackendProxyService::get_client_for_backend(int backend_fd) const {
    auto it = backend_connections_.find(backend_fd);
    if (it == backend_connections_.end()) {
        return {-1, -1};
    }
    return it->second;
}

}  // namespace titan::core
