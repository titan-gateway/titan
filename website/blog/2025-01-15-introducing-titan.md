---
slug: introducing-titan
title: Introducing Titan - The Fastest API Gateway Built for Modern Infrastructure
authors: [titan]
tags: [announcement, performance, architecture]
---

We're excited to introduce **Titan**, a next-generation API Gateway built from the ground up to deliver industry-leading performance while maintaining simplicity and developer-friendliness. After months of development and optimization, we're proud to share a proxy designed for exceptional throughput while maintaining sub-millisecond tail latency.

<!--truncate-->

## Why We Built Titan

The explosion of microservices architectures has created an interesting challenge: how do you route millions of requests per second between hundreds of services without the gateway becoming a bottleneck? Traditional API gateways like Nginx and Envoy have served us well for years, but we believed there was room to push performance even further by rethinking fundamental design decisions.

We started Titan with a simple question: what if we could eliminate every source of overhead in the request path? What if memory allocations, lock contention, and unnecessary data copying simply didn't exist in the hot path? This led us down a fascinating journey into modern systems programming, where we combined cutting-edge compiler features with battle-tested concurrency patterns to create something genuinely different.

## Performance That Changes the Game

In our benchmarks, Titan delivers exceptional throughput that outperforms traditional API gateways running the same workload. But raw throughput only tells part of the story. What really matters in production is consistency, and that's where Titan truly shines. Under sustained load with high concurrency, we maintain P99 latencies under 1 millisecond. That means 99% of your requests complete in less than a thousandth of a second, even when the system is hammered.

How do we achieve this? Through an obsessive focus on eliminating waste. Every request that flows through Titan uses zero-copy proxying—we never duplicate the request or response body in memory. The data flows from the client socket directly to the backend socket without ever being copied. This dramatically reduces CPU overhead and cache pressure, allowing a single core to handle far more traffic.

Our thread-per-core architecture takes this even further. Instead of using thread pools with locks and queues, we dedicate one OS thread to each physical CPU core. Each thread owns its own arena allocator, connection pool, and metrics counters. This shared-nothing design means threads never compete for resources, never wait on locks, and never experience cache contention from other cores. The result is near-linear scalability—double your cores, double your throughput.

## Architecture Built for Speed

The routing engine at the heart of Titan leverages SIMD (Single Instruction, Multiple Data) instructions to process path matching in parallel. When a request arrives for `/api/users/123`, we're not comparing the path character-by-character in a loop. Instead, we use vector instructions to compare multiple segments simultaneously, dramatically reducing the CPU cycles needed per request. Combined with a radix tree data structure, we can route requests to the correct backend in constant time, regardless of how many routes you've defined.

For modern applications using HTTP/2, Titan provides first-class support with connection multiplexing and server push. A single TCP connection can handle hundreds of concurrent streams, each representing a different request. This eliminates the overhead of TCP handshake negotiations and allows browsers to fetch resources in parallel without opening multiple connections.

Backend connection pooling is where many proxies fall short, but it's critical for performance. Establishing a new TCP connection and TLS handshake for every request adds milliseconds of latency. Titan maintains a pool of persistent connections to each backend, intelligently reusing them across requests. Health checks run continuously in the background, detecting failed connections before they impact real traffic. When a backend becomes unhealthy, Titan automatically routes traffic to healthy instances without dropping requests.

## Production-Ready from Day One

Performance means nothing if you can't operate the system in production. That's why Titan includes comprehensive operational features that make it production-ready today.

Configuration changes are a fact of life in microservices environments—you're constantly adding new routes, adjusting upstream pools, or tuning rate limits. With Titan, you can reload the configuration without dropping a single request. We use RCU (Read-Copy-Update) patterns borrowed from the Linux kernel: new requests immediately see the updated configuration, while in-flight requests complete using the old configuration. The transition is atomic and invisible to clients.

Graceful shutdown is another critical operational requirement. When you need to restart Titan for an upgrade or maintenance, you don't want to abruptly close thousands of active connections. Titan's shutdown process first stops accepting new connections, then waits for all in-flight requests to complete before exiting. Clients never see failed requests, and your metrics stay clean.

Observability is built into the core. Every Titan instance exports Prometheus-compatible metrics covering request rates, latency histograms, upstream connection pools, and error rates. You can graph these metrics in Grafana or alert on them with Prometheus Alertmanager. For Kubernetes deployments, we provide production-ready Helm charts with health checks, resource limits, and autoscaling configurations already configured.

Security isn't an afterthought either. Titan supports TLS 1.3 with modern cipher suites, allowing you to terminate SSL at the gateway and communicate with backends over plain HTTP within your private network. This offloads encryption overhead from your application servers while keeping external traffic secure.

## Middleware That Solves Real Problems

A proxy is only useful if it can handle the cross-cutting concerns every API needs: rate limiting, CORS, authentication, logging. Titan implements a two-phase middleware architecture inspired by systems like Kong and Nginx. Each request flows through request-phase middleware (authentication, rate limiting, request transformation) before being proxied to a backend. The response then flows through response-phase middleware (adding headers, logging, response transformation) before returning to the client.

Rate limiting is a perfect example of middleware done right. Titan implements token bucket rate limiting per client IP address, tracking request rates entirely in memory with lock-free atomic operations. If a client exceeds their limit, Titan returns an HTTP 429 response without ever contacting the backend. This protects your infrastructure from abusive clients while maintaining sub-millisecond latency for legitimate traffic.

CORS (Cross-Origin Resource Sharing) is another common pain point in modern web applications. Titan handles CORS preflight requests automatically, responding to OPTIONS requests with the appropriate headers before they reach your backend. You configure allowed origins, methods, and headers once in your Titan config, and it handles the rest.

Request and response logging provides visibility into every request flowing through your system. Titan logs the HTTP method, path, status code, and total duration for each request. Combined with structured logging, you can easily ship these logs to Elasticsearch or Loki for analysis.

## Getting Started Takes Minutes

We designed Titan to be trivial to deploy. For quick experimentation, our auto-detect installer handles everything:

```bash
curl -fsSL https://raw.githubusercontent.com/JonathanBerhe/titan/main/scripts/install-optimized.sh | bash
```

This script detects your platform (Linux or macOS), CPU architecture (x64 or ARM), and downloads the appropriate optimized binary. For containerized deployments, we publish official Docker images:

```bash
docker pull ghcr.io/JonathanBerhe/titan:latest
```

Configuration is intentionally simple. Here's a complete config that proxies all traffic to a backend server:

```json
{
  "server": { "port": 8080 },
  "upstreams": [
    {
      "name": "backend",
      "backends": [{ "host": "localhost", "port": 3000 }]
    }
  ],
  "routes": [
    {
      "path": "/*",
      "upstream": "backend"
    }
  ]
}
```

Start Titan with a single command:

```bash
titan --config config.json
```

That's it. No complex YAML manifests, no arcane configuration directives, no unexpected defaults. The entire configuration is self-explanatory JSON that any developer can read and understand.

## The Road Ahead

Titan is already production-ready, but we have ambitious plans for the future. Distributed tracing integration with OpenTelemetry is our top priority—imagine being able to trace a request from your frontend, through Titan, across multiple backend services, and back to the client. Every hop would be instrumented automatically, giving you complete visibility into where time is spent.

Advanced load balancing algorithms are also on the roadmap. While round-robin works well for homogeneous backends, real infrastructure is messier. Consistent hashing would allow session affinity without sticky sessions. Least-connections would automatically route traffic to the least-loaded backend. We're exploring how to implement these without sacrificing the zero-lock architecture that makes Titan fast.

WebSocket proxying is another frequently requested feature. Modern applications increasingly rely on WebSockets for real-time features like chat, notifications, and collaborative editing. Titan should be able to proxy these connections just as efficiently as HTTP requests.

Finally, gRPC transcoding would allow HTTP clients to call gRPC backends through Titan. This enables incremental migration from REST to gRPC without breaking existing clients, since Titan would handle the protocol translation transparently.

## Join the Community

Titan is open source and available on GitHub. We welcome contributions from the community, whether that's code, documentation, bug reports, or feature requests. Our GitHub Discussions is the best place to ask questions, share ideas, or show off what you're building with Titan.

You can find everything you need to get started in our documentation. The Quick Start Guide walks through installation and basic configuration, while the Architecture Overview explains the design decisions that make Titan fast. For production deployments, our deployment guides cover Docker, Kubernetes, and bare metal installations with security hardening and performance tuning recommendations.

We're excited to see what you build with Titan. Let's make API gateways fast again.

---

_Built with ❤️ by Jonathan Berhe_
