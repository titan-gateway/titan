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
  <a href="https://github.com/titan-gateway/titan/stargazers"><img src="https://img.shields.io/github/stars/titan-gateway/titan?style=social" alt="GitHub Stars"></a>
  <a href="https://github.com/titan-gateway/titan/releases"><img src="https://img.shields.io/github/v/release/titan-gateway/titan" alt="Latest Release"></a>
</p>

---

> **Actively Developed – Production Pilots Welcome**
>
> Titan is under active development with core features functional and extensively tested. While approaching v1.0, breaking changes may still occur. We welcome early adopters and production pilots – join our [Discussions](https://github.com/titan-gateway/titan/discussions) to share your experience.

---

## What is Titan?

Titan is an API gateway that sits between your clients and backend services, routing requests, load balancing traffic, and handling common tasks like rate limiting and CORS. Built with modern C++ for speed and efficiency, it's designed for applications that need to handle high traffic volumes with low latency.

Titan focuses on simplicity and performance—configure routes with a JSON file, reload without downtime, and monitor with built-in metrics. Whether you're running microservices, building APIs, or managing traffic for ML inference endpoints, Titan handles the routing so you can focus on your application.

---

## Performance

Titan is built for high-throughput, low-latency workloads with careful architectural choices:

**Key Performance Features:**
- **Thread-Per-Core Architecture**: Lock-free hot path, linear multi-core scaling
- **Zero-Copy I/O**: Minimal memory allocations during request processing
- **High-Performance Containers**: `fast_map` (ankerl::unordered_dense) provides 25% faster lookups, 400% faster iteration, and 30% less memory vs `std::unordered_map`
- **Connection Pooling**: Automatic backend connection reuse, prevents CLOSE-WAIT leaks
- **Efficient Event Loop**: epoll (Linux) / kqueue (macOS) for optimal I/O multiplexing

Initial benchmarks show competitive performance with industry-leading gateways. Formal benchmark results coming soon.

---

## Core Features

### Production-Ready Features

- **Modern Protocols**: HTTP/1.1, HTTP/2 with TLS 1.2/1.3 support (ALPN negotiation)
- **Advanced Routing**: Path-based routing with parameters (`/users/:id`) and wildcards (`/static/*`)
- **Load Balancing**: Round-robin with connection pooling, health checks drop dead backends automatically
- **Rate Limiting**: Token bucket algorithm, per-client IP enforcement (thread-local, no coordination)
- **CORS Support**: Preflight handling, configurable headers, credential support
- **Circuit Breaking**: Automatic backend failure detection with configurable thresholds
- **TLS Termination**: Handle HTTPS at edge (OpenSSL 3.x), communicate with backends over HTTP
- **Connection Pooling**: Automatic backend connection reuse with health checks
- **JWT Authentication & Authorization**: RS256/ES256/HS256 signature validation with thread-local caching (10k tokens/worker), per-route scope/role-based authorization (OAuth 2.0 scopes, RBAC)
- **Hot Reload**: Zero-downtime configuration updates via `SIGHUP` (RCU pattern)
- **Config Validation**: Comprehensive security hardening with fuzzy matching for typo detection, DoS protection, and detailed error reporting
- **Observability**: Prometheus metrics endpoint (`/metrics`), request/response logging
- **Request/Response Transformation**: Header manipulation, path rewriting with PCRE2 regex
- **Response Compression**: gzip/brotli/zstd with BREACH attack mitigation
- **WebSocket Proxying**: HTTP to WebSocket upgrade, bidirectional streaming

### In Development
- **gRPC Support**: Protocol detection, streaming support
- **Docker Service Discovery**: Auto-register containers as backends
- **Kubernetes Integration**: Ingress controller, Service/Endpoints API

### Planned Features

- **Advanced Load Balancing**: Least-connections, consistent-hash, sticky sessions
- **Retry & Timeout Logic**: Exponential backoff, configurable per-route
- **Response Caching**: In-memory LRU cache, Cache-Control support
- **OpenTelemetry Tracing**: Distributed tracing with Jaeger/Zipkin integration

---

## When to Use Titan

**Titan excels at:**
- **High-throughput APIs**: Gaming, real-time analytics, and applications requiring 100k+ req/s
- **Latency-sensitive applications**: Trading platforms, IoT gateways (sub-millisecond P99 latency)
- **AI/ML inference routing**: LLM load balancing, model versioning
- **Docker/Kubernetes environments**: Cloud-native deployments with service discovery

## Quick Start

Docker is easiest:

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

## Documentation

Full documentation is available at **[titan-gateway.github.io/titan](https://titan-gateway.github.io/titan/)**

- [Getting Started](https://titan-gateway.github.io/titan/docs/getting-started/installation)
- [Architecture Overview](https://titan-gateway.github.io/titan/docs/architecture/overview)
- [Configuration Guide](https://titan-gateway.github.io/titan/docs/configuration/overview)
- [Performance Tuning](https://titan-gateway.github.io/titan/docs/performance/optimization)
- [Deployment Options](https://titan-gateway.github.io/titan/docs/deployment/overview)

---

## Contributing

We welcome contributions! Here's how to get started:

### Good First Issues

Check our [good-first-issue](https://github.com/titan-gateway/titan/labels/good-first-issue) label for beginner-friendly tasks. Common areas:
- Middleware development (JWT, OAuth, caching)
- Documentation improvements
- Integration tests (pytest)
- Performance benchmarking

### Development Setup

1. **Clone the repository**:
   ```bash
   git clone https://github.com/titan-gateway/titan.git
   cd titan
   ```

2. **Build and test**:
   ```bash
   make dev      # Configure + build
   make test     # Run unit tests
   make format   # Format code (clang-format-21)
   ```

3. **Read the contribution guidelines** in [CONTRIBUTING.md](CONTRIBUTING.md)

### Code Standards

- **Language**: Modern C++ with Clang 18+ or GCC 14+
- **Style**: Google style (enforced by clang-format-21)
- **Testing**: All PRs must include unit tests (Catch2 framework)
- **Documentation**: Update documentation for significant changes

### Pull Request Process

1. Fork the repository and create a feature branch (`feat/your-feature`)
2. Write tests for new functionality
3. Ensure all tests pass (`make test`)
4. Format code (`make format`)
5. Submit PR with clear description referencing any related issues

---

## Community & Support

- **Discussions**: [GitHub Discussions](https://github.com/titan-gateway/titan/discussions) – Ask questions, share ideas
- **Issues**: [GitHub Issues](https://github.com/titan-gateway/titan/issues) – Report bugs, request features
- **Blog**: [titan-gateway.github.io/titan/blog](https://titan-gateway.github.io/titan/blog) – Technical deep-dives, updates

---

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

See [NOTICE](NOTICE) for full attribution and third-party license information.
