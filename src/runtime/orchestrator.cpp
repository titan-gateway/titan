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

// Titan Runtime Orchestrator - Implementation

#include "orchestrator.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../core/admin_server.hpp"
#include "../core/jwt_revocation.hpp"
#include "../core/logging.hpp"
#include "../core/socket.hpp"
#include "../gateway/factory.hpp"

using titan::core::close_fd;

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#endif

#include <atomic>
#include <thread>
#include <unordered_set>
#include <vector>

namespace titan::core {
extern std::atomic<bool> g_server_running;
extern std::atomic<bool> g_graceful_shutdown;
extern std::atomic<const gateway::UpstreamManager*> g_upstream_manager_for_metrics;
extern std::atomic<const gateway::CompressionMetrics*> g_compression_metrics_for_export;
}  // namespace titan::core

namespace titan::runtime {

// Global revocation queue for JWT token revocation (shared across all workers)
core::RevocationQueue* g_revocation_queue = nullptr;

// Worker thread function - runs dual epoll event loop for one worker
// Each worker has its own Server instance and TWO epoll/kqueue instances:
// - client_epoll: for client connections
// - backend_epoll: for backend connections (non-blocking proxy)
#ifdef __linux__
static void run_worker_thread(const control::Config& config, int worker_id) {
    // Pin thread to CPU core for better cache locality
    core::pin_thread_to_core(worker_id);

    // Initialize per-worker logger
    auto* logger = logging::init_worker_logger(worker_id, config.logging);

    // Build gateway components using factory
    auto router = gateway::build_router(config);
    auto upstream_manager = gateway::build_upstream_manager(config);
    auto upstream_manager_ptr = upstream_manager.get();

    // Build JWT components (per-worker)
    auto static_keys = gateway::build_static_key_manager(config);
    auto jwt_validator = gateway::build_jwt_validator(config, static_keys);
    if (jwt_validator) {
        auto jwks_fetcher = gateway::build_jwks_fetcher(config);
        if (jwks_fetcher) {
            jwt_validator->set_jwks_fetcher(jwks_fetcher);
            // Start JWKS fetcher background thread
            jwks_fetcher->start();
        }
    }

    auto pipeline =
        gateway::build_pipeline(config, upstream_manager_ptr, g_revocation_queue, jwt_validator);

    // Create server with pre-built components
    core::Server server(config, std::move(router), std::move(upstream_manager),
                        std::move(pipeline));
    server.set_logger(logger);
    if (auto ec = server.start(); ec) {
        return;
    }

    // Worker 0 shares its upstream_manager for admin server metrics
    if (worker_id == 0) {
        core::g_upstream_manager_for_metrics.store(server.upstream_manager(),
                                                   std::memory_order_release);
        core::g_compression_metrics_for_export.store(&gateway::compression_metrics,
                                                     std::memory_order_release);
    }

    int listen_fd = server.listen_fd();
    if (auto ec = core::set_nonblocking(listen_fd); ec) {
        return;
    }

    // Create TWO epoll instances (dual epoll pattern)
    int client_epoll_fd = epoll_create1(0);
    if (client_epoll_fd < 0)
        return;

    int backend_epoll_fd = server.backend_epoll_fd();
    if (backend_epoll_fd < 0) {
        close_fd(client_epoll_fd);
        return;
    }

    // Add listen socket to client epoll (edge-triggered)
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    if (epoll_ctl(client_epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        close_fd(client_epoll_fd);
        return;
    }

    std::unordered_set<int> active_client_fds;
    constexpr int MAX_EVENTS = 4096;
    epoll_event client_events[MAX_EVENTS];
    epoll_event backend_events[MAX_EVENTS];

    while (core::g_server_running) {
        // Process client events (with timeout to balance with backend processing)
        int n_client = epoll_wait(client_epoll_fd, client_events, MAX_EVENTS, 1);

        for (int i = 0; i < n_client; ++i) {
            int fd = client_events[i].data.fd;

            if (fd == listen_fd) {
                // Accept new connections
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd =
                        accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        continue;
                    }

                    if (auto ec = core::set_nonblocking(client_fd); ec) {
                        close_fd(client_fd);
                        continue;
                    }

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(client_addr.sin_port);

                    server.handle_accept(client_fd, ip_str, port);

                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    client_ev.data.fd = client_fd;
                    if (epoll_ctl(client_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == 0) {
                        active_client_fds.insert(client_fd);
                    } else {
                        server.handle_close(client_fd);
                    }
                }
            } else {
                // Handle client I/O
                if (client_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    epoll_ctl(client_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    active_client_fds.erase(fd);
                    server.handle_close(fd);
                } else if (client_events[i].events & EPOLLIN) {
                    server.handle_read(fd);
                }
            }
        }

        // Process backend events (with 1ms timeout)
        int n_backend = epoll_wait(backend_epoll_fd, backend_events, MAX_EVENTS, 1);

        for (int i = 0; i < n_backend; ++i) {
            int backend_fd = backend_events[i].data.fd;
            bool readable = backend_events[i].events & EPOLLIN;
            bool writable = backend_events[i].events & EPOLLOUT;
            bool error = backend_events[i].events & (EPOLLERR | EPOLLHUP);

            server.handle_backend_event(backend_fd, readable, writable, error);
        }

        // Process any pending backend operations
        server.process_backend_operations();
    }

    // Graceful shutdown: Wait for in-flight requests to complete
    if (core::g_graceful_shutdown && !active_client_fds.empty()) {
        constexpr int SHUTDOWN_TIMEOUT_MS = 30000;  // 30 seconds
        constexpr int POLL_INTERVAL_MS = 100;
        int elapsed_ms = 0;

        printf("Worker %d: Draining %zu active connections (timeout: %ds)...\n", worker_id,
               active_client_fds.size(), SHUTDOWN_TIMEOUT_MS / 1000);

        // Remove listen socket from epoll (stop accepting new connections)
        epoll_ctl(client_epoll_fd, EPOLL_CTL_DEL, listen_fd, nullptr);

        while (!active_client_fds.empty() && elapsed_ms < SHUTDOWN_TIMEOUT_MS) {
            // Continue processing existing connections
            int n_client = epoll_wait(client_epoll_fd, client_events, MAX_EVENTS, POLL_INTERVAL_MS);

            for (int i = 0; i < n_client; ++i) {
                int fd = client_events[i].data.fd;

                if (client_events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    epoll_ctl(client_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    active_client_fds.erase(fd);
                    server.handle_close(fd);
                } else if (client_events[i].events & EPOLLIN) {
                    server.handle_read(fd);
                }
            }

            // Process backend events
            int n_backend = epoll_wait(backend_epoll_fd, backend_events, MAX_EVENTS, 1);
            for (int i = 0; i < n_backend; ++i) {
                int backend_fd = backend_events[i].data.fd;
                bool readable = backend_events[i].events & EPOLLIN;
                bool writable = backend_events[i].events & EPOLLOUT;
                bool error = backend_events[i].events & (EPOLLERR | EPOLLHUP);
                server.handle_backend_event(backend_fd, readable, writable, error);
            }

            server.process_backend_operations();
            elapsed_ms += POLL_INTERVAL_MS;
        }

        if (active_client_fds.empty()) {
            printf("Worker %d: All connections drained successfully.\n", worker_id);
        } else {
            printf(
                "Worker %d: Shutdown timeout reached, %zu connections still active. Forcing "
                "close.\n",
                worker_id, active_client_fds.size());
        }
    }

    // Cleanup: Close remaining connections
    for (int fd : active_client_fds) {
        server.handle_close(fd);
    }
    close_fd(client_epoll_fd);
    server.stop();
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
static void run_worker_thread(const control::Config& config, int worker_id) {
    // Note: macOS doesn't support thread CPU affinity
    // core::pin_thread_to_core(worker_id);  // No-op on macOS

    // Build gateway components using factory
    auto router = gateway::build_router(config);
    auto upstream_manager = gateway::build_upstream_manager(config);
    auto upstream_manager_ptr = upstream_manager.get();

    // Build JWT components (per-worker)
    auto static_keys = gateway::build_static_key_manager(config);
    auto jwt_validator = gateway::build_jwt_validator(config, static_keys);
    if (jwt_validator) {
        auto jwks_fetcher = gateway::build_jwks_fetcher(config);
        if (jwks_fetcher) {
            jwt_validator->set_jwks_fetcher(jwks_fetcher);
            // Start JWKS fetcher background thread
            jwks_fetcher->start();
        }
    }

    auto pipeline =
        gateway::build_pipeline(config, upstream_manager_ptr, g_revocation_queue, jwt_validator);

    // Create server with pre-built components
    core::Server server(config, std::move(router), std::move(upstream_manager),
                        std::move(pipeline));
    if (auto ec = server.start(); ec) {
        return;
    }

    // Worker 0 shares its upstream_manager for admin server metrics
    if (worker_id == 0) {
        core::g_upstream_manager_for_metrics.store(server.upstream_manager(),
                                                   std::memory_order_release);
        core::g_compression_metrics_for_export.store(&gateway::compression_metrics,
                                                     std::memory_order_release);
    }

    int listen_fd = server.listen_fd();
    if (auto ec = core::set_nonblocking(listen_fd); ec) {
        return;
    }

    // Create TWO kqueue instances (dual kqueue pattern)
    int client_kq = kqueue();
    if (client_kq < 0)
        return;

    // Backend kqueue is managed by Server class
    int backend_kq = server.backend_epoll_fd();
    if (backend_kq < 0) {
        close_fd(client_kq);
        return;
    }

    // Add listen socket to client kqueue
    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(client_kq, &change, 1, nullptr, 0, nullptr) < 0) {
        close_fd(client_kq);
        return;
    }

    std::unordered_set<int> active_client_fds;
    constexpr int MAX_EVENTS = 4096;
    struct kevent client_events[MAX_EVENTS];
    struct kevent backend_events[MAX_EVENTS];
    struct timespec timeout{0, 1000000};  // 1ms timeout

    while (core::g_server_running) {
        // Process client events (immediate timeout)
        struct timespec zero_timeout{0, 0};
        int n_client = kevent(client_kq, nullptr, 0, client_events, MAX_EVENTS, &zero_timeout);

        for (int i = 0; i < n_client; ++i) {
            int fd = static_cast<int>(client_events[i].ident);

            if (fd == listen_fd) {
                // Accept new connections
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);
                    int client_fd =
                        accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        continue;
                    }

                    if (auto ec = core::set_nonblocking(client_fd); ec) {
                        close_fd(client_fd);
                        continue;
                    }

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(client_addr.sin_port);

                    server.handle_accept(client_fd, ip_str, port);

                    struct kevent client_ev;
                    EV_SET(&client_ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                    if (kevent(client_kq, &client_ev, 1, nullptr, 0, nullptr) == 0) {
                        active_client_fds.insert(client_fd);
                    } else {
                        server.handle_close(client_fd);
                    }
                }
            } else {
                // Handle client I/O
                if (client_events[i].flags & EV_EOF) {
                    active_client_fds.erase(fd);
                    server.handle_close(fd);
                } else if (client_events[i].filter == EVFILT_READ) {
                    server.handle_read(fd);
                }
            }
        }

        // Process backend events (1ms timeout)
        int n_backend = kevent(backend_kq, nullptr, 0, backend_events, MAX_EVENTS, &timeout);

        for (int i = 0; i < n_backend; ++i) {
            int backend_fd = static_cast<int>(backend_events[i].ident);
            bool readable = backend_events[i].filter == EVFILT_READ;
            bool writable = backend_events[i].filter == EVFILT_WRITE;
            bool error = backend_events[i].flags & EV_EOF || backend_events[i].flags & EV_ERROR;

            server.handle_backend_event(backend_fd, readable, writable, error);
        }

        // Process any pending backend operations
        server.process_backend_operations();
    }

    // Graceful shutdown: Wait for in-flight requests to complete
    if (core::g_graceful_shutdown && !active_client_fds.empty()) {
        constexpr int SHUTDOWN_TIMEOUT_MS = 30000;  // 30 seconds
        constexpr int POLL_INTERVAL_MS = 100;
        int elapsed_ms = 0;

        printf("Worker %d: Draining %zu active connections (timeout: %ds)...\n", worker_id,
               active_client_fds.size(), SHUTDOWN_TIMEOUT_MS / 1000);

        // Remove listen socket from kqueue (stop accepting new connections)
        struct kevent change;
        EV_SET(&change, listen_fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(client_kq, &change, 1, nullptr, 0, nullptr);

        while (!active_client_fds.empty() && elapsed_ms < SHUTDOWN_TIMEOUT_MS) {
            // Continue processing existing connections
            struct timespec poll_timeout{0, POLL_INTERVAL_MS * 1000000};  // Convert ms to ns
            int n_client = kevent(client_kq, nullptr, 0, client_events, MAX_EVENTS, &poll_timeout);

            for (int i = 0; i < n_client; ++i) {
                int fd = static_cast<int>(client_events[i].ident);

                if (client_events[i].flags & EV_EOF) {
                    active_client_fds.erase(fd);
                    server.handle_close(fd);
                } else if (client_events[i].filter == EVFILT_READ) {
                    server.handle_read(fd);
                }
            }

            // Process backend events
            int n_backend = kevent(backend_kq, nullptr, 0, backend_events, MAX_EVENTS, &timeout);
            for (int i = 0; i < n_backend; ++i) {
                int backend_fd = static_cast<int>(backend_events[i].ident);
                bool readable = backend_events[i].filter == EVFILT_READ;
                bool writable = backend_events[i].filter == EVFILT_WRITE;
                bool error = backend_events[i].flags & EV_EOF || backend_events[i].flags & EV_ERROR;
                server.handle_backend_event(backend_fd, readable, writable, error);
            }

            server.process_backend_operations();
            elapsed_ms += POLL_INTERVAL_MS;
        }

        if (active_client_fds.empty()) {
            printf("Worker %d: All connections drained successfully.\n", worker_id);
        } else {
            printf(
                "Worker %d: Shutdown timeout reached, %zu connections still active. Forcing "
                "close.\n",
                worker_id, active_client_fds.size());
        }
    }

    // Cleanup: Close remaining connections
    for (int fd : active_client_fds) {
        server.handle_close(fd);
    }
    close_fd(client_kq);
    server.stop();
}
#endif

#ifdef __linux__
// Linux epoll-based event loop (O(1) scalability)
std::error_code run_simple_server(const control::Config& config) {
    // Build gateway components using factory
    auto router = gateway::build_router(config);
    auto upstream_manager = gateway::build_upstream_manager(config);
    auto upstream_manager_ptr = upstream_manager.get();

    // Build JWT components
    auto static_keys = gateway::build_static_key_manager(config);
    auto jwt_validator = gateway::build_jwt_validator(config, static_keys);
    if (jwt_validator) {
        auto jwks_fetcher = gateway::build_jwks_fetcher(config);
        if (jwks_fetcher) {
            jwt_validator->set_jwks_fetcher(jwks_fetcher);
            // Start JWKS fetcher background thread
            jwks_fetcher->start();
        }
    }

    auto pipeline =
        gateway::build_pipeline(config, upstream_manager_ptr, g_revocation_queue, jwt_validator);

    // Create server with pre-built components
    core::Server server(config, std::move(router), std::move(upstream_manager),
                        std::move(pipeline));

    if (auto ec = server.start(); ec) {
        return ec;
    }

    int listen_fd = server.listen_fd();
    if (auto ec = core::set_nonblocking(listen_fd); ec) {
        return ec;
    }

    // Create epoll instance for client connections
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        return std::error_code(errno, std::system_category());
    }

    // Get backend epoll fd (managed by Server class)
    int backend_epoll_fd = server.backend_epoll_fd();
    if (backend_epoll_fd < 0) {
        close_fd(epoll_fd);
        return std::error_code(errno, std::system_category());
    }

    // Add listen socket to epoll (edge-triggered for performance)
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        close_fd(epoll_fd);
        return std::error_code(errno, std::system_category());
    }

    core::g_server_running = true;
    std::unordered_set<int> active_fds;

    // Increased from 128 to 4096 for better scalability under extreme load
    // With 5000 concurrent connections, we can now process more events per epoll_wait call
    constexpr int MAX_EVENTS = 4096;
    epoll_event events[MAX_EVENTS];
    epoll_event backend_events[MAX_EVENTS];

    while (core::g_server_running) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (n_events < 0) {
            if (errno == EINTR)
                continue;
            close_fd(epoll_fd);
            return std::error_code(errno, std::system_category());
        }

        for (int i = 0; i < n_events; ++i) {
            int fd = events[i].data.fd;

            // Handle new connections on listen socket
            if (fd == listen_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);

                    int client_fd =
                        accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // No more connections to accept
                        }
                        continue;  // Error, try next
                    }

                    if (auto ec = core::set_nonblocking(client_fd); ec) {
                        close_fd(client_fd);
                        continue;
                    }

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(client_addr.sin_port);

                    server.handle_accept(client_fd, ip_str, port);

                    // Add client socket to epoll (edge-triggered)
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    client_ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == 0) {
                        active_fds.insert(client_fd);
                    } else {
                        server.handle_close(client_fd);
                    }
                }
            }
            // Handle client socket events
            else {
                if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    // Connection closed or error
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    active_fds.erase(fd);
                    server.handle_close(fd);
                } else if (events[i].events & EPOLLIN) {
                    // Data available to read
                    server.handle_read(fd);
                }
            }
        }

        // Process backend events (with 1ms timeout)
        int n_backend = epoll_wait(backend_epoll_fd, backend_events, MAX_EVENTS, 1);

        for (int i = 0; i < n_backend; ++i) {
            int backend_fd = backend_events[i].data.fd;
            bool readable = backend_events[i].events & EPOLLIN;
            bool writable = backend_events[i].events & EPOLLOUT;
            bool error = backend_events[i].events & (EPOLLERR | EPOLLHUP);

            server.handle_backend_event(backend_fd, readable, writable, error);
        }

        // Process any pending backend operations (async proxy support)
        server.process_backend_operations();
    }

    // Cleanup
    for (int fd : active_fds) {
        server.handle_close(fd);
    }
    close_fd(epoll_fd);
    server.stop();
    return {};
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
// macOS/BSD kqueue-based event loop (O(1) scalability)
std::error_code run_simple_server(const control::Config& config) {
    // Build gateway components using factory
    auto router = gateway::build_router(config);
    auto upstream_manager = gateway::build_upstream_manager(config);
    auto upstream_manager_ptr = upstream_manager.get();

    // Build JWT components
    auto static_keys = gateway::build_static_key_manager(config);
    auto jwt_validator = gateway::build_jwt_validator(config, static_keys);
    if (jwt_validator) {
        auto jwks_fetcher = gateway::build_jwks_fetcher(config);
        if (jwks_fetcher) {
            jwt_validator->set_jwks_fetcher(jwks_fetcher);
            // Start JWKS fetcher background thread
            jwks_fetcher->start();
        }
    }

    auto pipeline =
        gateway::build_pipeline(config, upstream_manager_ptr, g_revocation_queue, jwt_validator);

    // Create server with pre-built components
    core::Server server(config, std::move(router), std::move(upstream_manager),
                        std::move(pipeline));

    if (auto ec = server.start(); ec) {
        return ec;
    }

    int listen_fd = server.listen_fd();
    if (auto ec = core::set_nonblocking(listen_fd); ec) {
        return ec;
    }

    // Create kqueue for client connections
    int kq = kqueue();
    if (kq < 0) {
        return std::error_code(errno, std::system_category());
    }

    // Get backend kqueue fd (managed by Server class)
    int backend_kq = server.backend_epoll_fd();
    if (backend_kq < 0) {
        close_fd(kq);
        return std::error_code(errno, std::system_category());
    }

    // Add listen socket to kqueue
    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kq, &change, 1, nullptr, 0, nullptr) < 0) {
        close_fd(kq);
        return std::error_code(errno, std::system_category());
    }

    core::g_server_running = true;
    std::unordered_set<int> active_fds;

    // Increased from 128 to 4096 for better scalability under extreme load
    constexpr int MAX_EVENTS = 4096;
    struct kevent events[MAX_EVENTS];
    struct kevent backend_events[MAX_EVENTS];
    struct timespec timeout{1, 0};                // 1 second timeout
    struct timespec backend_timeout{0, 1000000};  // 1ms timeout

    while (core::g_server_running) {
        int n_events = kevent(kq, nullptr, 0, events, MAX_EVENTS, &timeout);

        if (n_events < 0) {
            if (errno == EINTR)
                continue;
            close_fd(kq);
            return std::error_code(errno, std::system_category());
        }

        for (int i = 0; i < n_events; ++i) {
            int fd = static_cast<int>(events[i].ident);

            // Handle new connections
            if (fd == listen_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);

                    int client_fd =
                        accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        continue;
                    }

                    if (auto ec = core::set_nonblocking(client_fd); ec) {
                        close_fd(client_fd);
                        continue;
                    }

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(client_addr.sin_port);

                    server.handle_accept(client_fd, ip_str, port);

                    // Add client to kqueue
                    struct kevent client_ev;
                    EV_SET(&client_ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
                    if (kevent(kq, &client_ev, 1, nullptr, 0, nullptr) == 0) {
                        active_fds.insert(client_fd);
                    } else {
                        server.handle_close(client_fd);
                    }
                }
            }
            // Handle client events
            else {
                if (events[i].flags & EV_EOF) {
                    // Connection closed
                    active_fds.erase(fd);
                    server.handle_close(fd);
                } else if (events[i].filter == EVFILT_READ) {
                    // Data available
                    server.handle_read(fd);
                }
            }
        }

        // Process backend events (with 1ms timeout)
        int n_backend =
            kevent(backend_kq, nullptr, 0, backend_events, MAX_EVENTS, &backend_timeout);

        for (int i = 0; i < n_backend; ++i) {
            int backend_fd = static_cast<int>(backend_events[i].ident);
            bool readable = backend_events[i].filter == EVFILT_READ;
            bool writable = backend_events[i].filter == EVFILT_WRITE;
            bool error = backend_events[i].flags & EV_EOF;

            server.handle_backend_event(backend_fd, readable, writable, error);
        }

        // Process any pending backend operations (async proxy support)
        server.process_backend_operations();
    }

    // Cleanup
    for (int fd : active_fds) {
        server.handle_close(fd);
    }
    close_fd(kq);
    server.stop();
    return {};
}
#endif

// Multi-threaded server with SO_REUSEPORT load balancing
std::error_code run_multi_threaded_server(const control::Config& config) {
    // Determine number of worker threads (default to CPU core count)
    uint32_t num_workers = config.server.worker_threads;
    if (num_workers == 0) {
        num_workers = core::get_cpu_count();
    }

    // Set server running flag
    core::g_server_running = true;

    // Initialize global revocation queue for JWT token revocation
    // This is shared across all workers for synchronization
    core::RevocationQueue revocation_queue;
    g_revocation_queue = &revocation_queue;

    // Start admin server on separate port (metrics, health)
    // This runs on port 9090 (configurable) and is NOT exposed to public
    std::unique_ptr<core::AdminServer> admin_server;
    std::thread admin_thread;

    if (config.metrics.enabled) {
        // Admin server will read upstream_manager from global once worker 0 sets it
        // Pass revocation queue for JWT revocation endpoint
        admin_server = std::make_unique<core::AdminServer>(config, nullptr, &revocation_queue);

        auto err = admin_server->start();
        if (err) {
            std::fprintf(stderr, "Failed to start admin server on port %u: %s\n",
                         config.metrics.port, err.message().c_str());
            // Continue anyway - metrics are optional
        } else {
            std::printf("Admin server listening on 127.0.0.1:%u (metrics, health)\n",
                        config.metrics.port);

            // Run admin server in separate thread
            admin_thread = std::thread([&admin_server]() { admin_server->run(); });
        }
    }

    // Spawn worker threads
    std::vector<std::thread> workers;
    workers.reserve(num_workers);

    for (uint32_t i = 0; i < num_workers; ++i) {
        workers.emplace_back([&config, i]() { run_worker_thread(config, i); });
    }

    // Wait for all workers to finish
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    // Stop admin server
    if (admin_server) {
        admin_server->stop();
        if (admin_thread.joinable()) {
            admin_thread.join();
        }
    }

    // Cleanup global revocation queue pointer
    g_revocation_queue = nullptr;

    return {};
}

}  // namespace titan::runtime
