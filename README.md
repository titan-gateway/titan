<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="./assets/titan-lockup-dark-400x128.png">
    <source media="(prefers-color-scheme: light)" srcset="./assets/titan-lockup-light-400x128.png">
    <img alt="Titan" src="./assets/titan-lockup-light-400x128.png" width="400">
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

## Why Titan?

Modern microservices demand API gateways that can handle massive throughput without becoming bottlenecks. Titan is built from the ground up to eliminate every source of overhead in the request path—no wasted CPU cycles, no memory allocations, no lock contention.

The result? **Exceptional throughput with P99 latencies under 1 millisecond**. Every request flows through zero-copy proxying, with data moving directly from client socket to backend socket without duplication. Thread-per-core architecture means each CPU core owns its resources independently—no locks, no queues, no cache contention.

## Key Features

### Performance Without Compromise

**Thread-Per-Core Architecture:** Each CPU core runs a dedicated worker thread that owns its arena allocator, connection pool, and metrics. This shared-nothing design eliminates synchronization overhead and delivers near-linear scalability—double your cores, double your throughput.

**Zero-Copy Proxying:** Request and response bodies never get copied in memory. Data flows directly from client to backend, dramatically reducing CPU overhead and memory pressure.

**Connection Pooling:** Persistent connections to backends eliminate TCP handshake overhead. Background health checks detect failures before they impact traffic, automatically routing around unhealthy instances.

### Production-Ready Operations

**Hot Configuration Reload:** Update routes, upstreams, and middleware without dropping a single request. RCU (Read-Copy-Update) patterns ensure new requests see updated config while in-flight requests complete with the old config.

**Graceful Shutdown:** Clean exits that wait for all in-flight requests to complete before terminating. No abrupt connection closures, no failed requests.

**Full Observability:** Prometheus-compatible metrics for request rates, latency histograms, connection pool sizes, and error rates. Structured logging integrates with Elasticsearch, Loki, or CloudWatch.

### Modern Protocol Support

**HTTP/1.1 & HTTP/2:** Full support for both protocols with automatic ALPN negotiation. HTTP/2 multiplexing allows hundreds of concurrent streams over a single TCP connection.

**TLS 1.2/1.3:** Modern cipher suites with OpenSSL 3.x. Terminate SSL at the gateway and communicate with backends over plain HTTP within your private network.

### Developer-Friendly

**Simple JSON Configuration:** No complex YAML manifests or arcane directives. The entire configuration is self-explanatory JSON that any developer can read and understand.

**Middleware Pipeline:** Two-phase architecture (request/response) with built-in support for rate limiting, CORS, authentication, and custom header injection.

## Quick Start

### Run with Docker (Recommended)

The fastest way to try Titan is using the pre-built Docker image:

```bash
# Pull the latest image
docker pull ghcr.io/jonathanberhe/titan:latest

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
  ghcr.io/jonathanberhe/titan:latest

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

## Architecture

See [CLAUDE.md](CLAUDE.md) for detailed architecture documentation.

## Development Roadmap

See [ROADMAP.md](ROADMAP.md) for the development plan and progress tracking.

## Benchmarking

See [benchmark/README.md](benchmark/README.md) for benchmarking instructions.

## Performance

**Design Goals:**
- **Throughput:** Designed to outperform traditional API gateways through zero-copy proxying and lock-free architecture
- **Latency:** P99 <1ms under sustained load with thread-per-core design
- **Reliability:** Connection pooling with health checks and zero-downtime reloads

See [SCALABILITY_ANALYSIS.md](SCALABILITY_ANALYSIS.md) for detailed benchmark methodology and results.

## Documentation

Full documentation is available at **[jonathanberhe.github.io/titan](https://jonathanberhe.github.io/titan/)**

- [Getting Started](https://jonathanberhe.github.io/titan/docs/getting-started/installation)
- [Architecture Overview](https://jonathanberhe.github.io/titan/docs/architecture/overview)
- [Configuration Guide](https://jonathanberhe.github.io/titan/docs/configuration/overview)
- [Deployment Options](https://jonathanberhe.github.io/titan/docs/deployment/overview)

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
