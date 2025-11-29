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

I got tired of API gateways that either perform well but lack features, or have every feature but add 5ms of overhead per request. Titan is my attempt at building something that does both: handle 100k+ requests per second while staying under 1ms P99 latency.

It's written in C++23 and uses a thread-per-core architecture where each worker thread owns its memory, connection pool, and routing table. No shared state means no locks in the hot path. Requests get zero-copy proxied from client socket to backend socket—no intermediate buffers, no allocations.

## What Can You Do With It?

**Reverse Proxy** - Load balance across backends (round-robin, least-connections, weighted, random). Connection pooling keeps backend connections warm. Health checks drop dead backends automatically.

**API Gateway** - Route by URL path with parameters (`/users/:id`) and wildcards (`/static/*`). Add CORS headers, enforce rate limits, inject authentication. Reload config with `kill -HUP` without dropping connections.

**TLS Termination** - Handle HTTPS at the edge (TLS 1.2/1.3 with OpenSSL 3.x), talk to backends over plain HTTP internally. ALPN negotiates HTTP/2 vs HTTP/1.1 automatically.

**Rate Limiting** - Token bucket algorithm, per-IP limits. Each worker enforces limits locally, no cross-thread coordination.

**Observability** - Prometheus metrics endpoint, structured JSON logs, health checks for Kubernetes liveness/readiness probes.

**Hot Reload** - Edit config file, send `SIGHUP`, changes apply instantly. In-flight requests finish with old config, new requests use new config (RCU pattern).

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

## Performance

Tested on ARM64 Linux (4 cores), proxying to a local Nginx backend:

- **HTTP/2 (TLS):** 118k req/s, 666μs mean latency
- **HTTP/1.1:** 190k req/s, 642μs mean latency

P99 stays under 1ms during sustained load. Connection pool prevents CLOSE-WAIT leaks (validated with 1M+ request tests). No locks in request path, no allocations after startup.

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
# Test CI workflow
