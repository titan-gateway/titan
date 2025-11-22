// Titan Server Runner - Implementation

#include "server_runner.hpp"
#include "socket.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#endif

#include <vector>
#include <atomic>
#include <unordered_set>

namespace titan::core {

extern std::atomic<bool> g_server_running;

// Worker thread function - runs event loop for one worker
// Each worker has its own Server instance and epoll/kqueue instance
#ifdef __linux__
static void run_worker_thread(const control::Config& config, int worker_id);
#elif defined(__APPLE__) || defined(__FreeBSD__)
static void run_worker_thread(const control::Config& config, int worker_id);
#endif

#ifdef __linux__
// Linux epoll-based event loop (O(1) scalability)
std::error_code run_simple_server(const control::Config& config) {
    Server server(config);

    if (auto ec = server.start(); ec) {
        return ec;
    }

    int listen_fd = server.listen_fd();
    if (auto ec = set_nonblocking(listen_fd); ec) {
        return ec;
    }

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        return std::error_code(errno, std::system_category());
    }

    // Add listen socket to epoll (edge-triggered for performance)
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        close(epoll_fd);
        return std::error_code(errno, std::system_category());
    }

    g_server_running = true;
    std::unordered_set<int> active_fds;

    // Increased from 128 to 4096 for better scalability under extreme load
    // With 5000 concurrent connections, we can now process more events per epoll_wait call
    constexpr int MAX_EVENTS = 4096;
    epoll_event events[MAX_EVENTS];

    while (g_server_running) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);

        if (n_events < 0) {
            if (errno == EINTR) continue;
            close(epoll_fd);
            return std::error_code(errno, std::system_category());
        }

        for (int i = 0; i < n_events; ++i) {
            int fd = events[i].data.fd;

            // Handle new connections on listen socket
            if (fd == listen_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);

                    int client_fd = accept(listen_fd,
                        reinterpret_cast<sockaddr*>(&client_addr),
                        &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // No more connections to accept
                        }
                        continue;  // Error, try next
                    }

                    if (auto ec = set_nonblocking(client_fd); ec) {
                        close(client_fd);
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
                }
                else if (events[i].events & EPOLLIN) {
                    // Data available to read
                    server.handle_read(fd);
                }
            }
        }
    }

    // Cleanup
    for (int fd : active_fds) {
        server.handle_close(fd);
    }
    close(epoll_fd);
    server.stop();
    return {};
}

#elif defined(__APPLE__) || defined(__FreeBSD__)
// macOS/BSD kqueue-based event loop (O(1) scalability)
std::error_code run_simple_server(const control::Config& config) {
    Server server(config);

    if (auto ec = server.start(); ec) {
        return ec;
    }

    int listen_fd = server.listen_fd();
    if (auto ec = set_nonblocking(listen_fd); ec) {
        return ec;
    }

    // Create kqueue
    int kq = kqueue();
    if (kq < 0) {
        return std::error_code(errno, std::system_category());
    }

    // Add listen socket to kqueue
    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kq, &change, 1, nullptr, 0, nullptr) < 0) {
        close(kq);
        return std::error_code(errno, std::system_category());
    }

    g_server_running = true;
    std::unordered_set<int> active_fds;

    // Increased from 128 to 4096 for better scalability under extreme load
    constexpr int MAX_EVENTS = 4096;
    struct kevent events[MAX_EVENTS];
    struct timespec timeout{1, 0};  // 1 second timeout

    while (g_server_running) {
        int n_events = kevent(kq, nullptr, 0, events, MAX_EVENTS, &timeout);

        if (n_events < 0) {
            if (errno == EINTR) continue;
            close(kq);
            return std::error_code(errno, std::system_category());
        }

        for (int i = 0; i < n_events; ++i) {
            int fd = static_cast<int>(events[i].ident);

            // Handle new connections
            if (fd == listen_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t addr_len = sizeof(client_addr);

                    int client_fd = accept(listen_fd,
                        reinterpret_cast<sockaddr*>(&client_addr),
                        &addr_len);

                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        continue;
                    }

                    if (auto ec = set_nonblocking(client_fd); ec) {
                        close(client_fd);
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
                }
                else if (events[i].filter == EVFILT_READ) {
                    // Data available
                    server.handle_read(fd);
                }
            }
        }
    }

    // Cleanup
    for (int fd : active_fds) {
        server.handle_close(fd);
    }
    close(kq);
    server.stop();
    return {};
}
#endif

} // namespace titan::core
