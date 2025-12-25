---
sidebar_position: 1
title: Architecture Overview
---

# Architecture Overview

Titan's architecture emerged from a simple question: how can we eliminate every unnecessary CPU cycle, memory copy, and context switch from the request path? The answer is a carefully orchestrated design where every component—from threading to memory management—works together to maximize throughput and minimize latency.

## Design Philosophy

Traditional API gateways treat performance as an optimization problem to be solved after the architecture is established. Titan inverts this: performance is the architecture. Every design decision prioritizes the hot path—the journey of a request from client to backend and back again—ensuring it runs with minimal overhead.

## Thread-Per-Core: Eliminating Lock Contention

Most high-performance systems eventually hit a wall: lock contention. When threads fight over shared mutexes to access connection pools, routing tables, or memory allocators, performance degrades non-linearly with core count. Add more CPUs, and throughput barely improves—or worse, decreases.

Titan solves this with **thread-per-core (TPC)** architecture. Each CPU core runs exactly one OS thread, pinned to that core via `sched_setaffinity` (Linux) or `pthread_setaffinity_np` (macOS). That thread owns everything it needs:

- **Event loop** (epoll on Linux, kqueue on macOS) for non-blocking I/O
- **Connection pool** with dedicated backend connections
- **Memory arena** for per-request allocations
- **Routing table** (a thread-local copy via RCU)
- **Metrics counters** (lock-free per-thread stats)

Because each thread is self-contained, there's **zero lock contention**. No mutexes, no atomic operations in the hot path, no cache-line ping-pong between cores. This enables linear scaling: double the cores, double the throughput.

## Shared-Nothing: Isolation by Design

Workers in Titan don't communicate unless absolutely necessary. Configuration updates use RCU (Read-Copy-Update) to atomically swap routing tables without coordination. Connection pools remain thread-local—each worker maintains its own pool of backend connections, avoiding the need to share or steal connections from other threads.

This shared-nothing philosophy extends to memory: each request allocates from a thread-local arena that's reset after the request completes. No global allocator contention, no heap fragmentation across threads.

## Zero-Copy: Minimizing Memory Operations

Every `memcpy` is a missed opportunity. Titan uses modern C++ views and spans to eliminate unnecessary copies:

**`std::string_view`** represents HTTP headers, paths, and query parameters without copying. When parsing a request, Titan creates views into the receive buffer rather than allocating new strings. These views remain valid for the request lifetime, then disappear when the arena resets.

**`std::span`** handles request and response bodies. Instead of copying body data into intermediate buffers, Titan forwards spans directly to backend connections—zero-copy proxying from client socket to backend socket.

**Direct buffer forwarding** means TLS-encrypted data goes through OpenSSL's buffers directly to the backend connection without intermediate staging. Combined with TCP_NODELAY and carefully tuned socket buffers, this minimizes both latency and memory pressure.

## SIMD: Vectorized Performance

Modern CPUs offer SIMD (Single Instruction, Multiple Data) instructions that process multiple data elements in parallel. Titan leverages these for critical hot-path operations:

**Route matching** uses AVX2 (x86) or NEON (ARM) instructions to compare path prefixes against multiple routes simultaneously. Instead of checking routes one-by-one, Titan can test 8-16 routes per instruction.

**Header comparison** for common headers (Host, Content-Type, etc.) uses vectorized string comparison, significantly faster than byte-by-byte loops.

**URL parsing** benefits from SIMD-accelerated character classification—quickly identifying path separators, query delimiters, and special characters without branching.

**WebSocket frame unmasking** uses SIMD to XOR payloads with 4-byte masking keys at wire speed. Every client-to-server WebSocket frame must be unmasked per RFC 6455. SIMD processes 16-32 bytes per cycle (vs 1 byte for scalar), providing 16-30x speedup for real-time applications where every frame requires unmasking.

## Request Lifecycle

Understanding how a request flows through Titan reveals how these architectural choices combine:

1. **Accept**: The event loop receives a client connection via epoll/kqueue
2. **Parse**: llhttp or nghttp2 parses headers into `string_view` objects backed by the receive buffer
3. **Route**: SIMD-accelerated radix tree lookup finds the matching route in O(log n) time
4. **Middleware**: Request passes through the middleware pipeline (CORS, rate limiting, etc.)
5. **Pool Acquire**: Thread-local connection pool returns an existing backend connection in O(1)
6. **Proxy**: Request data is forwarded using zero-copy `span` objects
7. **Response**: Backend response flows back through middleware, then to the client
8. **Cleanup**: Arena memory is reset in bulk; connection returns to pool

The entire path—from receiving bytes to forwarding them—involves zero global locks, minimal allocations, and maximum CPU cache efficiency.

## Performance Characteristics

These algorithmic complexities define Titan's runtime behavior:

| Operation           | Complexity | Notes |
| ------------------- | ---------- | ----- |
| Route matching      | O(log n)   | Radix tree with SIMD prefix matching |
| Request parsing     | O(n)       | Linear scan with llhttp state machine |
| Connection pool get | O(1)       | LIFO stack of healthy connections |
| Config reload       | O(1)       | Atomic pointer swap via RCU |
| Middleware chain    | O(m)       | Linear execution, m = number of middleware |

The key insight: operations that happen on every request (routing, pooling) are constant or logarithmic time. Expensive operations (parsing) are unavoidable and optimized with SIMD.

## Technology Stack

Titan builds on battle-tested libraries while maintaining full control over the hot path:

**Language**: Modern C++ with standard library containers and views. No exceptions in hot path; `std::expected` for error handling.

**HTTP Parsing**: **llhttp** (HTTP/1.1) provides a zero-copy, state-machine-based parser originally developed for Node.js. **nghttp2** (HTTP/2) handles frame parsing and multiplexing.

**TLS/SSL**: **OpenSSL 3.6.0** with ALPN support for HTTP/2 negotiation. Titan uses OpenSSL's async APIs to integrate with the event loop without blocking.

**JSON Configuration**: **Glaze** offers compile-time reflection and SIMD-accelerated parsing, making config reloads nearly instant even for large configurations.

**Memory Allocation**: **mimalloc** replaces the default allocator globally, providing better performance and memory efficiency than glibc malloc.

## Next Steps

Now that you understand Titan's architecture, explore how to put it to work:

- **[Configuration Reference](../configuration/overview.md)** - Learn how to configure routes, upstreams, and middleware
- **[Quick Start Guide](../getting-started/quickstart.md)** - Deploy your first Titan instance
- **[Deployment Guide](../deployment/overview.md)** - Production deployment patterns
- **[Contributing](../contributing/getting-started.md)** - Dive into the codebase and contribute
