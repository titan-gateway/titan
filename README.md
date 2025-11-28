<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./assets/titan-lockup-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="./assets/titan-lockup-light.svg">
    <img alt="Titan" src="./assets/titan-lockup-light.svg" width="400">
  </picture>
</p>

<p align="center">
  <strong>High-Performance API Gateway Built for Modern Infrastructure</strong>
</p>

<p align="center">
  <a href="https://titan-gateway.github.io/titan/">Documentation</a> •
  <a href="https://titan-gateway.github.io/titan/blog">Blog</a> •
  <a href="https://github.com/titan-gateway/titan/discussions">Discussions</a>
</p>

<p align="center">
  <a href="https://opensource.org/licenses/Apache-2.0"><img src="https://img.shields.io/badge/License-Apache%202.0-blue.svg" alt="License"></a>
  <a href="https://github.com/titan-gateway/titan/actions"><img src="https://github.com/titan-gateway/titan/workflows/CI/badge.svg" alt="Build Status"></a>
  <a href="https://titan-gateway.github.io/titan/"><img src="https://img.shields.io/badge/docs-latest-blue.svg" alt="Documentation"></a>
</p>

---

> **⚠️ Early Development Notice**
>
> Titan is currently in active development and has not yet reached v1.0. While core features are functional and extensively tested, the project is still evolving. Breaking changes may occur, and we recommend against production deployments until the v1.0 stable release.
>
> We welcome early adopters, contributors, and feedback from the community as we work toward a stable release.

---

## Why Titan?

Modern microservices demand API gateways that can handle massive throughput without becoming bottlenecks. Titan is built from the ground up to eliminate every source of overhead in the request path—no wasted CPU cycles, no memory allocations, no lock contention.

The result? **Exceptional throughput with P99 latencies under 1 millisecond**. Every request flows through zero-copy proxying, with data moving directly from client socket to backend socket without duplication. Thread-per-core architecture means each CPU core owns its resources independently—no locks, no queues, no cache contention.

## What Can Titan Do?

Titan serves multiple roles in modern infrastructure. Choose the capabilities that match your needs:

### Reverse Proxy & Load Balancer

Distribute traffic across multiple backend servers with intelligent load balancing strategies. Titan supports round-robin, least-connections, random, and weighted round-robin algorithms. Connection pooling maintains persistent connections to backends, eliminating TCP handshake overhead on every request. Health checks automatically detect and route around failed instances, ensuring high availability without manual intervention.

### API Gateway

Route incoming requests to different backend services based on URL paths, with support for path parameters and wildcard patterns. The middleware pipeline enables request validation, transformation, and response manipulation. Built-in CORS handling, rate limiting, and authentication hooks provide the building blocks for securing and managing your API ecosystem. Hot configuration reloads allow you to add routes or modify upstreams without restarting the gateway or dropping active connections.

### TLS Termination

Handle TLS encryption at the network edge using modern cipher suites from OpenSSL 3.x. Titan supports TLS 1.2 and 1.3 with automatic ALPN protocol negotiation, allowing clients to connect via HTTPS while your internal backend services communicate over plain HTTP within your private network. This offloads cryptographic operations from your application servers and centralizes certificate management.

### Rate Limiting & Traffic Control

Protect your backend services from overload with per-IP rate limiting using token bucket algorithms. Rate limits are enforced locally on each worker thread, providing consistent protection without distributed coordination overhead. Configure different limits per route or globally across all traffic.

### HTTP/2 & HTTP/1.1 Gateway

Support both HTTP/1.1 and HTTP/2 on the same port with automatic protocol detection. For TLS connections, ALPN negotiation selects the optimal protocol. For cleartext connections, Titan detects the HTTP/2 connection preface. HTTP/2 multiplexing allows hundreds of concurrent streams over a single TCP connection, reducing connection overhead for modern web applications.

### Observability & Monitoring

Export Prometheus-compatible metrics for request rates, latency histograms, connection pool statistics, and per-route error rates. Health check endpoints provide Kubernetes-ready liveness and readiness probes. Structured logging captures request details for integration with Elasticsearch, Loki, or CloudWatch.

### Zero-Downtime Operations

Update configuration files and reload them with a SIGHUP signal—routes, upstreams, and middleware changes take effect immediately without dropping in-flight requests. Graceful shutdown on SIGTERM waits for active requests to complete before terminating, ensuring clean deployments in Kubernetes rolling updates or systemd service restarts.

## Quick Start

### Run with Docker (Recommended)

The fastest way to try Titan is using the pre-built Docker image:

```bash
# Pull the latest image
docker pull ghcr.io/titan-gateway/titan:latest

# Create a simple configuration
cat > config.json <<EOF
{
  "server": { "port": 8080 },
  "upstreams": [{
    "name": "backend",
    "backends": [{ "host": "httpbin.org", "port": 443, "tls": true }]
  }],
  "routes": [{ "path": "/*", "upstream": "backend" }]
}
EOF

# Run Titan
docker run -d -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json \
  --name titan \
  ghcr.io/titan-gateway/titan:latest

# Test it
curl http://localhost:8080/get
```

### Building from Source

Requirements:
- Clang 18+ or GCC 14+
- CMake 3.28+
- vcpkg
- GNU Make

```bash
# Development build
make dev

# Release build
make release

# Run tests
make test

# See all available commands
make help
```

## Benchmarking

See [benchmark/README.md](benchmark/README.md) for benchmarking instructions.

## Performance

Titan is engineered to deliver exceptional performance in production environments where latency and throughput are critical. The architecture achieves P99 latencies under 1 millisecond under sustained load, ensuring predictable response times even during traffic spikes. This performance characteristic stems from careful architectural decisions that eliminate common bottlenecks found in traditional API gateways.

The gateway maintains this low-latency performance while handling substantial throughput through efficient resource utilization. Connection pooling to backend services eliminates the overhead of repeated TCP handshakes, while integrated health checking ensures traffic is only routed to healthy instances. Configuration updates happen without service interruption, allowing teams to deploy changes confidently without scheduled maintenance windows.

## Documentation

Full documentation is available at **[titan-gateway.github.io/titan](https://titan-gateway.github.io/titan/)**

- [Getting Started](https://titan-gateway.github.io/titan/docs/getting-started/installation)
- [Architecture Overview](https://titan-gateway.github.io/titan/docs/architecture/overview)
- [Configuration Guide](https://titan-gateway.github.io/titan/docs/configuration/overview)
- [Deployment Options](https://titan-gateway.github.io/titan/docs/deployment/overview)

## License

Titan is licensed under the [Apache License 2.0](LICENSE).

```
Copyright 2025 Titan Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

### Third-Party Licenses

Titan includes the following open-source components:

- [mimalloc](https://github.com/microsoft/mimalloc) - MIT License
- [OpenSSL](https://www.openssl.org/) - Apache License 2.0
- [Catch2](https://github.com/catchorg/Catch2) - Boost Software License 1.0
- [glaze](https://github.com/stephenberry/glaze) - MIT License
- [{fmt}](https://github.com/fmtlib/fmt) - MIT License
- [llhttp](https://github.com/nodejs/llhttp) - MIT License
- [nghttp2](https://github.com/nghttp2/nghttp2) - MIT License

See [NOTICE](NOTICE) for full attribution and license information.
