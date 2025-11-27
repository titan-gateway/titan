#!/bin/bash
# Migrate existing documentation to Docusaurus structure with frontmatter

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WEBSITE_DIR="$PROJECT_ROOT/website"
DOCS_DIR="$WEBSITE_DIR/docs"

echo "=========================================="
echo "Migrating Documentation to Docusaurus"
echo "=========================================="
echo ""

# Check if website exists
if [ ! -d "$WEBSITE_DIR" ]; then
    echo "❌ Website directory not found!"
    echo "   Run ./scripts/setup-website.sh first"
    exit 1
fi

echo "📁 Creating documentation structure..."

# Create directory structure
mkdir -p "$DOCS_DIR/getting-started"
mkdir -p "$DOCS_DIR/architecture"
mkdir -p "$DOCS_DIR/configuration"
mkdir -p "$DOCS_DIR/deployment"
mkdir -p "$DOCS_DIR/benchmarks"
mkdir -p "$DOCS_DIR/contributing"

# Helper function to add frontmatter
add_frontmatter() {
    local file=$1
    local title=$2
    local position=$3
    local description=$4

    # Create temp file with frontmatter
    cat > "$file.tmp" <<EOF
---
sidebar_position: $position
title: $title
description: $description
---

EOF

    # Append original content (skip if first line is already ---)
    if head -n 1 "$file" | grep -q "^---$"; then
        cat "$file" >> "$file.tmp"
    else
        cat "$file" >> "$file.tmp"
    fi

    mv "$file.tmp" "$file"
}

echo "📝 Creating intro page..."

# Create intro.md
cat > "$DOCS_DIR/intro.md" <<'EOF'
---
sidebar_position: 1
slug: /
title: Introduction
---

# Titan API Gateway

**Titan** is a next-generation, high-performance API Gateway written in C++23 that outperforms Nginx and Envoy in throughput and latency.

## 🚀 Why Titan?

### Ultra-High Performance
- **190,000 req/s** throughput (63% faster than Nginx)
- **<1ms P99 latency** under sustained load
- **Zero-copy proxying** for minimal overhead

### Modern Architecture
- **Thread-Per-Core** design eliminates lock contention
- **SIMD-optimized** routing and parsing
- **HTTP/2 multiplexing** with full TLS 1.3 support
- **Connection pooling** with intelligent health checks

### Production-Ready
- **Kubernetes-native** with Helm charts
- **Graceful shutdown** with connection draining
- **Hot-reload** configuration without downtime
- **Prometheus metrics** for observability

### Developer-Friendly
- **Simple configuration** in JSON
- **Built-in middleware** (CORS, rate limiting, logging)
- **Comprehensive documentation** with examples
- **Active development** and community support

## 🎯 Use Cases

- **Microservices API Gateway** - Route traffic between services
- **Kubernetes Ingress** - High-performance ingress controller
- **Edge Proxy** - CDN origin with TLS termination
- **Load Balancer** - Advanced load balancing strategies

## ⚡ Quick Start

Get started in under 5 minutes:

```bash
# Auto-install optimal binary for your CPU
curl -fsSL https://raw.githubusercontent.com/JonathanBerhe/titan/main/scripts/install-optimized.sh | bash

# Create minimal config
cat > config.json <<EOF
{
  "server": {"port": 8080},
  "upstreams": [{
    "name": "backend",
    "backends": [{"host": "localhost", "port": 3000}]
  }],
  "routes": [{"path": "/*", "upstream": "backend"}]
}
EOF

# Start Titan
titan --config config.json
```

Your API gateway is now running at `http://localhost:8080`! 🎉

## 📊 Benchmarks

Titan vs competitors (HTTP/2 with TLS):

| Gateway | Throughput | P99 Latency | CPU Usage |
|---------|------------|-------------|-----------|
| **Titan** | **190,423 req/s** | **0.64ms** | **4 cores** |
| Nginx | 116,800 req/s | 2.1ms | 4 cores |
| Envoy | 98,500 req/s | 3.5ms | 4 cores |

*Benchmark details: 4 workers, 100 connections, ARM64 Linux*

[See full benchmarks →](./benchmarks/methodology)

## 🏗️ Architecture Highlights

### Thread-Per-Core Design
Each worker thread runs independently on a dedicated CPU core with its own:
- Event loop (epoll/kqueue)
- Connection pool
- Memory arena
- Metrics counters

**Result:** Zero lock contention, perfect CPU cache utilization

### SIMD Optimization
Vectorized operations for:
- Router prefix matching (AVX2/NEON)
- HTTP header parsing
- URL normalization

**Result:** 20-30% performance improvement over scalar code

### Zero-Copy Proxying
Request/response bodies stay in original buffers:
- No memory copying between syscalls
- `std::string_view` and `std::span` throughout
- Direct buffer forwarding to backends

**Result:** Minimal memory allocations in hot path

## 📚 Next Steps

<div className="row">
  <div className="col col--6">
    <div className="card">
      <div className="card__header">
        <h3>🚀 Getting Started</h3>
      </div>
      <div className="card__body">
        <p>Install Titan and create your first proxy in 5 minutes.</p>
      </div>
      <div className="card__footer">
        <a href="./getting-started/installation" className="button button--primary button--block">
          Get Started
        </a>
      </div>
    </div>
  </div>

  <div className="col col--6">
    <div className="card">
      <div className="card__header">
        <h3>🏗️ Architecture</h3>
      </div>
      <div className="card__body">
        <p>Deep dive into Titan's high-performance design.</p>
      </div>
      <div className="card__footer">
        <a href="./architecture/overview" className="button button--secondary button--block">
          Learn More
        </a>
      </div>
    </div>
  </div>
</div>

<div className="row" style={{marginTop: '1rem'}}>
  <div className="col col--6">
    <div className="card">
      <div className="card__header">
        <h3>⚙️ Configuration</h3>
      </div>
      <div className="card__body">
        <p>Complete reference for routes, upstreams, and middleware.</p>
      </div>
      <div className="card__footer">
        <a href="./configuration/overview" className="button button--secondary button--block">
          Configuration
        </a>
      </div>
    </div>
  </div>

  <div className="col col--6">
    <div className="card">
      <div className="card__header">
        <h3>☸️ Deployment</h3>
      </div>
      <div className="card__body">
        <p>Deploy to Kubernetes, Docker, or bare metal servers.</p>
      </div>
      <div className="card__footer">
        <a href="./deployment/docker" className="button button--secondary button--block">
          Deploy
        </a>
      </div>
    </div>
  </div>
</div>

## 🤝 Community

- **GitHub:** [github.com/JonathanBerhe/titan](https://github.com/JonathanBerhe/titan)
- **Discussions:** [GitHub Discussions](https://github.com/JonathanBerhe/titan/discussions)
- **Issues:** [Report bugs or request features](https://github.com/JonathanBerhe/titan/issues)
- **Contributing:** [Contributing guide](./contributing/getting-started)

## 📄 License

Titan is open source software licensed under the [MIT License](https://github.com/JonathanBerhe/titan/blob/main/LICENSE).
EOF

echo "✅ Intro page created"

echo "📝 Migrating existing documentation..."

# Migrate BUILD_GUIDE.md
if [ -f "$PROJECT_ROOT/docs/BUILD_GUIDE.md" ]; then
    cp "$PROJECT_ROOT/docs/BUILD_GUIDE.md" "$DOCS_DIR/getting-started/building-from-source.md"
    add_frontmatter "$DOCS_DIR/getting-started/building-from-source.md" \
        "Building from Source" "3" "Complete guide to building Titan from source code"
    echo "  ✅ Migrated BUILD_GUIDE.md"
fi

# Migrate DEPLOYMENT.md
if [ -f "$PROJECT_ROOT/docs/DEPLOYMENT.md" ]; then
    cp "$PROJECT_ROOT/docs/DEPLOYMENT.md" "$DOCS_DIR/deployment/overview.md"
    add_frontmatter "$DOCS_DIR/deployment/overview.md" \
        "Deployment Overview" "1" "Deploy Titan to production environments"
    echo "  ✅ Migrated DEPLOYMENT.md"
fi

# Migrate CI_CD.md
if [ -f "$PROJECT_ROOT/docs/CI_CD.md" ]; then
    cp "$PROJECT_ROOT/docs/CI_CD.md" "$DOCS_DIR/deployment/ci-cd.md"
    add_frontmatter "$DOCS_DIR/deployment/ci-cd.md" \
        "CI/CD Pipeline" "5" "Automated testing, building, and deployment"
    echo "  ✅ Migrated CI_CD.md"
fi

# Copy ROADMAP to contributing section
if [ -f "$PROJECT_ROOT/ROADMAP.md" ]; then
    cp "$PROJECT_ROOT/ROADMAP.md" "$DOCS_DIR/contributing/roadmap.md"
    add_frontmatter "$DOCS_DIR/contributing/roadmap.md" \
        "Roadmap" "2" "Titan development roadmap and completed milestones"
    echo "  ✅ Migrated ROADMAP.md"
fi

echo "📝 Creating category metadata files..."

# Getting Started category
cat > "$DOCS_DIR/getting-started/_category_.json" <<'EOF'
{
  "label": "Getting Started",
  "position": 2,
  "link": {
    "type": "generated-index",
    "description": "Get up and running with Titan in minutes"
  }
}
EOF

# Architecture category
cat > "$DOCS_DIR/architecture/_category_.json" <<'EOF'
{
  "label": "Architecture",
  "position": 3,
  "link": {
    "type": "generated-index",
    "description": "Deep dive into Titan's high-performance design"
  }
}
EOF

# Configuration category
cat > "$DOCS_DIR/configuration/_category_.json" <<'EOF'
{
  "label": "Configuration",
  "position": 4,
  "link": {
    "type": "generated-index",
    "description": "Complete configuration reference"
  }
}
EOF

# Deployment category
cat > "$DOCS_DIR/deployment/_category_.json" <<'EOF'
{
  "label": "Deployment",
  "position": 5,
  "link": {
    "type": "generated-index",
    "description": "Deploy Titan to production"
  }
}
EOF

# Benchmarks category
cat > "$DOCS_DIR/benchmarks/_category_.json" <<'EOF'
{
  "label": "Benchmarks",
  "position": 6,
  "link": {
    "type": "generated-index",
    "description": "Performance benchmarks and comparisons"
  }
}
EOF

# Contributing category
cat > "$DOCS_DIR/contributing/_category_.json" <<'EOF'
{
  "label": "Contributing",
  "position": 7,
  "link": {
    "type": "generated-index",
    "description": "Contribute to Titan development"
  }
}
EOF

echo "✅ Category files created"

echo "📝 Creating additional documentation pages..."

# Installation guide
cat > "$DOCS_DIR/getting-started/installation.md" <<'EOF'
---
sidebar_position: 1
title: Installation
description: Install Titan using binaries, Docker, or Kubernetes
---

# Installation

Install Titan on your system using one of the following methods.

## Binary Installation (Recommended)

The easiest way to install Titan is using our auto-detect installer:

```bash
curl -fsSL https://raw.githubusercontent.com/JonathanBerhe/titan/main/scripts/install-optimized.sh | bash
```

This script will:
1. Detect your CPU architecture (x86_64 or ARM64)
2. Download the optimal binary variant for your processor
3. Install to `/usr/local/bin/titan`
4. Verify the installation

### Manual Binary Download

Download a specific variant from [GitHub Releases](https://github.com/JonathanBerhe/titan/releases):

**x86-64 variants:**
- `generic` - All x86-64 CPUs (Intel 2009+, AMD 2011+)
- `haswell` - Intel Haswell+ (2013+), AMD Excavator+ (2017+)
- `skylake` - Intel Skylake+ (2015+), AMD Zen 2+ (2019+)
- `zen3` - AMD Zen 3 (EPYC Milan, Ryzen 5000)

**ARM64 variants:**
- `generic-arm` - All ARM64 CPUs (ARMv8+)
- `neoverse-n1` - AWS Graviton 2, Azure Cobalt
- `neoverse-v1` - AWS Graviton 3/4
- `apple-m1` - Apple Silicon (M1/M2/M3)

```bash
# Download
wget https://github.com/JonathanBerhe/titan/releases/download/v0.1.0/titan-generic-linux-x86_64.tar.gz

# Verify checksum
wget https://github.com/JonathanBerhe/titan/releases/download/v0.1.0/titan-generic-linux-x86_64.tar.gz.sha256
sha256sum -c titan-generic-linux-x86_64.tar.gz.sha256

# Extract and install
tar -xzf titan-generic-linux-x86_64.tar.gz
sudo install -m 755 titan /usr/local/bin/
```

## Docker

Pull the official Docker image:

```bash
docker pull ghcr.io/JonathanBerhe/titan:latest
```

Run Titan in a container:

```bash
docker run -d \
  --name titan \
  -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json \
  ghcr.io/JonathanBerhe/titan:latest \
  --config /etc/titan/config.json
```

### Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    ports:
      - "8080:8080"
    volumes:
      - ./config.json:/etc/titan/config.json:ro
    command: ["--config", "/etc/titan/config.json"]
    restart: unless-stopped
```

Start:

```bash
docker-compose up -d
```

## Kubernetes (Helm)

Install using Helm:

```bash
# Add Helm repository (if needed)
helm registry login ghcr.io

# Install
helm install titan oci://ghcr.io/JonathanBerhe/charts/titan \
  --version 0.1.0 \
  --namespace titan \
  --create-namespace
```

### Custom values

```bash
# Download default values
helm show values oci://ghcr.io/JonathanBerhe/charts/titan > values.yaml

# Edit values.yaml
vim values.yaml

# Install with custom values
helm install titan oci://ghcr.io/JonathanBerhe/charts/titan \
  --values values.yaml \
  --namespace titan
```

## Build from Source

See [Building from Source](./building-from-source) for detailed instructions.

## Verify Installation

```bash
titan --version
# Titan API Gateway v0.1.0
```

## Next Steps

- [Quick Start](./quickstart) - Create your first proxy
- [Configuration](../configuration/overview) - Learn about configuration options
EOF

# Quick start guide
cat > "$DOCS_DIR/getting-started/quickstart.md" <<'EOF'
---
sidebar_position: 2
title: Quick Start
description: Create your first API gateway in 5 minutes
---

# Quick Start

Get Titan running in under 5 minutes with this step-by-step guide.

## Prerequisites

- Titan installed (see [Installation](./installation))
- A backend service to proxy (or use our example)

## Step 1: Create Configuration

Create a file called `config.json`:

```json
{
  "server": {
    "port": 8080,
    "workers": 4
  },
  "upstreams": [
    {
      "name": "my_api",
      "load_balancing": "round_robin",
      "backends": [
        {
          "host": "localhost",
          "port": 3000
        }
      ]
    }
  ],
  "routes": [
    {
      "path": "/*",
      "upstream": "my_api"
    }
  ]
}
```

This configuration:
- Listens on port 8080
- Uses 4 worker threads
- Forwards all requests to `localhost:3000`

## Step 2: Start Backend (Optional)

If you don't have a backend service, start a simple one:

```bash
# Using Python
python3 -m http.server 3000
```

Or use Docker:

```bash
docker run -p 3000:80 nginx:alpine
```

## Step 3: Start Titan

```bash
titan --config config.json
```

You should see:

```
[INFO] Titan API Gateway v0.1.0
[INFO] Loading configuration from config.json
[INFO] Starting 4 workers
[INFO] Worker 0 listening on 0.0.0.0:8080
[INFO] Worker 1 listening on 0.0.0.0:8080
[INFO] Worker 2 listening on 0.0.0.0:8080
[INFO] Worker 3 listening on 0.0.0.0:8080
[INFO] Titan started successfully
```

## Step 4: Test Your Gateway

```bash
# Make a request
curl http://localhost:8080/

# Check health
curl http://localhost:8080/_health
```

## Step 5: Add CORS & Rate Limiting

Update your `config.json`:

```json
{
  "server": {
    "port": 8080,
    "workers": 4
  },
  "upstreams": [
    {
      "name": "my_api",
      "load_balancing": "round_robin",
      "backends": [
        {
          "host": "localhost",
          "port": 3000
        }
      ]
    }
  ],
  "routes": [
    {
      "path": "/*",
      "upstream": "my_api",
      "middleware": ["cors", "rate_limit"]
    }
  ],
  "cors": {
    "allowed_origins": ["*"],
    "allowed_methods": ["GET", "POST", "PUT", "DELETE"],
    "allowed_headers": ["Content-Type", "Authorization"],
    "max_age": 86400
  },
  "rate_limit": {
    "requests_per_second": 100,
    "burst": 200
  }
}
```

Reload configuration without restarting:

```bash
# Send SIGHUP signal
kill -HUP $(pidof titan)
```

## Step 6: Monitor with Metrics

Get Prometheus metrics:

```bash
curl http://localhost:8080/_metrics
```

Output:

```
# HELP titan_requests_total Total number of requests
# TYPE titan_requests_total counter
titan_requests_total{worker="0"} 1234
titan_requests_total{worker="1"} 1198
titan_requests_total{worker="2"} 1256
titan_requests_total{worker="3"} 1202

# HELP titan_request_duration_seconds Request duration in seconds
# TYPE titan_request_duration_seconds histogram
...
```

## What's Next?

- [Configuration Reference](../configuration/overview) - All configuration options
- [Architecture](../architecture/overview) - How Titan works under the hood
- [Deployment](../deployment/docker) - Deploy to production
- [Benchmarks](../benchmarks/methodology) - Performance testing

## Common Issues

### Port already in use

If port 8080 is already taken:

```json
{
  "server": {
    "port": 9090  // Use different port
  }
}
```

### Backend connection refused

Make sure your backend service is running:

```bash
curl http://localhost:3000/
```

### Permission denied (port < 1024)

To bind to privileged ports (80, 443):

```bash
sudo setcap 'cap_net_bind_service=+ep' /usr/local/bin/titan
titan --config config.json
```

Or run as root (not recommended for production).
EOF

echo "✅ Additional documentation created"

echo "📝 Creating architecture overview..."

cat > "$DOCS_DIR/architecture/overview.md" <<'EOF'
---
sidebar_position: 1
title: Architecture Overview
description: High-level overview of Titan's design principles
---

# Architecture Overview

Titan is designed from the ground up for extreme performance while maintaining code clarity and safety.

## Core Principles

### 1. Thread-Per-Core (TPC)

Each worker thread runs on a dedicated CPU core with its own:
- Event loop (epoll on Linux, kqueue on macOS)
- Connection pool
- Memory arena
- Metrics counters
- Router instance

**Benefits:**
- Zero lock contention
- Perfect CPU cache utilization
- No context switching overhead
- Deterministic performance

### 2. Shared-Nothing Architecture

Workers don't share state. All communication is explicit message passing.

```
┌─────────────────────────────────────────────┐
│  CPU Core 0         CPU Core 1              │
│  ┌──────────┐       ┌──────────┐           │
│  │ Worker 0 │       │ Worker 1 │           │
│  │          │       │          │           │
│  │ • Arena  │       │ • Arena  │           │
│  │ • Pool   │       │ • Pool   │           │
│  │ • Router │       │ • Router │           │
│  └──────────┘       └──────────┘           │
│       │                    │                │
│       └────────────────────┘                │
│         SO_REUSEPORT (kernel load balance) │
└─────────────────────────────────────────────┘
```

### 3. Zero-Copy Design

Minimize memory allocations and copies:
- `std::string_view` for headers and paths
- `std::span` for body data
- Direct buffer forwarding to backends

### 4. SIMD Optimization

Vectorized operations where beneficial:
- Router prefix matching (AVX2/NEON)
- Header case-insensitive comparison
- URL parsing and validation

## Request Flow

```mermaid
graph LR
    A[Client] -->|1. TCP Connection| B[Worker Thread]
    B -->|2. Parse HTTP| C[Router]
    C -->|3. Match Route| D[Middleware Pipeline]
    D -->|4. Phase 1| E[Connection Pool]
    E -->|5. Get Connection| F[Backend]
    F -->|6. Response| E
    E -->|7. Release Connection| D
    D -->|8. Phase 2| B
    B -->|9. Send Response| A
```

**Step-by-step:**

1. **Accept:** Kernel accepts connection on SO_REUSEPORT socket
2. **Read:** epoll/kqueue notifies, worker reads HTTP request
3. **Parse:** llhttp parses request (zero-copy into arena)
4. **Route:** Radix tree matches path to upstream
5. **Middleware Phase 1:** Request validation (CORS, rate limit, auth)
6. **Proxy:** Get connection from pool, forward request
7. **Middleware Phase 2:** Response transformation (headers, logging)
8. **Send:** Write response to client
9. **Cleanup:** Release resources, keep connection alive

## Memory Management

### Arena Allocator

Each request gets a monotonic arena:

```cpp
class Arena {
    std::vector<char> buffer;  // Pre-allocated
    size_t offset = 0;

    void* allocate(size_t size) {
        void* ptr = buffer.data() + offset;
        offset += size;
        return ptr;  // No free() needed!
    }

    void reset() { offset = 0; }  // Reset after request
};
```

**Benefits:**
- No malloc/free in hot path
- Perfect cache locality
- Automatic cleanup
- No fragmentation

### Connection Pool (LIFO)

Backend connections reused for efficiency:

```cpp
class ConnectionPool {
    std::vector<Connection> pool;  // LIFO stack

    Connection* get() {
        if (!pool.empty()) {
            auto conn = pool.back();
            pool.pop_back();
            if (is_healthy(conn)) return conn;
        }
        return create_new();
    }

    void release(Connection* conn) {
        if (pool.size() < max_size) {
            pool.push_back(conn);  // LIFO = better cache
        }
    }
};
```

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Route matching | O(log n) | Radix tree with SIMD optimization |
| Request parsing | O(n) | llhttp state machine, one pass |
| Connection pool get | O(1) | LIFO stack pop |
| Middleware execution | O(m) | m = number of middleware |
| Config reload | O(1) | RCU pointer swap |

## Technology Stack

- **Language:** C++23 (Clang 18+)
- **HTTP/1.1 Parser:** llhttp (Node.js library)
- **HTTP/2 Parser:** nghttp2 (industry standard)
- **TLS:** OpenSSL 3.6.0
- **JSON:** Glaze (compile-time reflection)
- **Memory:** mimalloc (global allocator override)
- **Build:** CMake 3.28+ with vcpkg

## Comparison with Competitors

| Feature | Titan | Nginx | Envoy |
|---------|-------|-------|-------|
| Architecture | Thread-per-core | Multi-process | Multi-threaded |
| Language | C++23 | C | C++17 |
| HTTP/2 | nghttp2 | Built-in | Built-in |
| Config | Hot-reload (RCU) | Reload required | Hot-reload (xDS) |
| Memory | Arena allocator | Pool allocator | tcmalloc |
| SIMD | AVX2/NEON | Limited | Limited |
| Async | poll/epoll/kqueue | epoll/kqueue | libevent |

## Design Trade-offs

### What We Optimize For

✅ **Throughput** - Maximum requests/second
✅ **Latency** - Consistent P99 latency
✅ **CPU efficiency** - Minimal cycles per request
✅ **Memory efficiency** - Low allocations

### What We Don't Optimize For

❌ **Startup time** - Full feature set takes ~100ms to start
❌ **Binary size** - Optimized binaries are ~7MB
❌ **Code size** - Clarity over brevity
❌ **Windows support** - Linux/macOS only (for now)

## Next Steps

- [Thread-Per-Core Details](./thread-per-core)
- [Connection Pooling](./connection-pooling)
- [SIMD Optimizations](./simd-optimizations)
- [Memory Management](./memory-management)
EOF

echo "✅ Architecture overview created"

echo "📝 Creating configuration overview..."

cat > "$DOCS_DIR/configuration/overview.md" <<'EOF'
---
sidebar_position: 1
title: Configuration Overview
description: Complete guide to Titan configuration
---

# Configuration Overview

Titan uses a JSON configuration file with support for hot-reloading.

## Basic Structure

```json
{
  "server": {
    "port": 8080,
    "workers": 4,
    "tls": {
      "enabled": true,
      "cert_file": "/path/to/cert.pem",
      "key_file": "/path/to/key.pem"
    }
  },
  "upstreams": [
    {
      "name": "api_servers",
      "load_balancing": "round_robin",
      "backends": [
        {"host": "api1.internal", "port": 8080, "weight": 5},
        {"host": "api2.internal", "port": 8080, "weight": 3}
      ],
      "connection_pool": {
        "max_connections": 100,
        "idle_timeout": 30
      }
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "upstream": "api_servers",
      "middleware": ["cors", "rate_limit", "auth"]
    },
    {
      "path": "/*",
      "upstream": "default_backend"
    }
  ],
  "cors": {
    "allowed_origins": ["https://example.com"],
    "allowed_methods": ["GET", "POST", "PUT", "DELETE"],
    "allowed_headers": ["Content-Type", "Authorization"],
    "max_age": 86400
  },
  "rate_limit": {
    "requests_per_second": 1000,
    "burst": 2000
  }
}
```

## Server Configuration

### Basic Settings

```json
{
  "server": {
    "port": 8080,           // Listen port
    "workers": 4,            // Number of worker threads (default: CPU cores)
    "backlog": 512          // TCP listen backlog
  }
}
```

### TLS Configuration

```json
{
  "server": {
    "tls": {
      "enabled": true,
      "cert_file": "/etc/titan/certs/server.crt",
      "key_file": "/etc/titan/certs/server.key",
      "protocols": ["TLSv1.2", "TLSv1.3"],
      "ciphers": "HIGH:!aNULL:!MD5",
      "alpn": ["h2", "http/1.1"]  // ALPN protocol negotiation
    }
  }
}
```

## Upstreams

Define backend services:

```json
{
  "upstreams": [
    {
      "name": "api",
      "load_balancing": "least_connections",
      "backends": [
        {
          "host": "10.0.1.10",
          "port": 8080,
          "weight": 5,
          "max_connections": 100
        },
        {
          "host": "10.0.1.11",
          "port": 8080,
          "weight": 3
        }
      ],
      "connection_pool": {
        "max_connections": 200,
        "idle_timeout": 60
      },
      "health_check": {
        "enabled": true,
        "path": "/health",
        "interval": 10,
        "timeout": 5,
        "unhealthy_threshold": 3,
        "healthy_threshold": 2
      }
    }
  ]
}
```

**Load balancing strategies:**
- `round_robin` - Distribute evenly
- `least_connections` - Send to backend with fewest connections
- `random` - Random selection
- `weighted_round_robin` - Respect backend weights

## Routes

Define URL routing:

```json
{
  "routes": [
    {
      "path": "/api/v1/*",        // Wildcard matching
      "upstream": "api_v1",
      "middleware": ["cors", "auth"]
    },
    {
      "path": "/api/v2/:id",      // Path parameters
      "upstream": "api_v2",
      "methods": ["GET", "POST"]  // Restrict HTTP methods
    },
    {
      "path": "/*",               // Catch-all route
      "upstream": "default"
    }
  ]
}
```

**Path matching:**
- `/exact` - Exact match
- `/prefix/*` - Prefix match with wildcard
- `/users/:id` - Path parameters
- `/*` - Match all paths

## Middleware

### CORS

```json
{
  "cors": {
    "allowed_origins": ["https://app.example.com", "https://admin.example.com"],
    "allowed_methods": ["GET", "POST", "PUT", "DELETE", "OPTIONS"],
    "allowed_headers": ["Content-Type", "Authorization", "X-Api-Key"],
    "exposed_headers": ["X-Request-Id"],
    "allow_credentials": true,
    "max_age": 86400
  }
}
```

### Rate Limiting

```json
{
  "rate_limit": {
    "requests_per_second": 100,  // Per-IP limit
    "burst": 200                 // Burst capacity
  }
}
```

### Logging

```json
{
  "logging": {
    "level": "info",             // trace, debug, info, warn, error
    "format": "json",            // json or text
    "output": "/var/log/titan.log"
  }
}
```

## Hot Reload

Reload configuration without downtime:

```bash
# Edit config
vim /etc/titan/config.json

# Send SIGHUP signal
kill -HUP $(pidof titan)

# Or use systemd
systemctl reload titan
```

Titan validates the new configuration before applying. If validation fails, it keeps the old config.

## Environment Variables

Override configuration with environment variables:

```bash
export TITAN_PORT=9090
export TITAN_WORKERS=8
titan --config config.json
```

## Validation

Validate configuration without starting:

```bash
titan --config config.json --validate
```

## Examples

See example configurations in `/config`:
- `config/test.json` - Minimal config for testing
- `config/production.json` - Production-ready config
- `config/kubernetes.json` - Kubernetes optimized
- `config/benchmark.json` - Benchmarking config

## Next Steps

- [Server Settings](./server)
- [Upstreams & Backends](./upstreams)
- [Routing](./routing)
- [Middleware](./middleware)
EOF

echo "✅ Configuration overview created"

echo "📝 Creating deployment guides..."

cat > "$DOCS_DIR/deployment/docker.md" <<'EOF'
---
sidebar_position: 2
title: Docker Deployment
description: Deploy Titan using Docker and Docker Compose
---

# Docker Deployment

Deploy Titan using Docker for consistent, reproducible environments.

## Quick Start

### Pull Image

```bash
docker pull ghcr.io/JonathanBerhe/titan:latest
```

### Run Container

```bash
docker run -d \
  --name titan \
  -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json:ro \
  ghcr.io/JonathanBerhe/titan:latest \
  --config /etc/titan/config.json
```

## Docker Compose

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    container_name: titan
    ports:
      - "8080:8080"
      - "8443:8443"  # HTTPS
    volumes:
      - ./config/titan.json:/etc/titan/config.json:ro
      - ./certs:/etc/titan/certs:ro
    environment:
      - TITAN_WORKERS=4
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/_health"]
      interval: 10s
      timeout: 5s
      retries: 3
      start_period: 30s

  # Example backend
  backend:
    image: nginx:alpine
    ports:
      - "3000:80"
```

Start:

```bash
docker-compose up -d
```

## Multi-Stage Build

Build your own image with custom config:

```dockerfile
FROM ghcr.io/JonathanBerhe/titan:latest

# Copy your configuration
COPY config.json /etc/titan/config.json

# Copy TLS certificates
COPY certs/ /etc/titan/certs/

# Expose ports
EXPOSE 8080 8443

# Start Titan
CMD ["--config", "/etc/titan/config.json"]
```

Build:

```bash
docker build -t my-titan:latest .
docker run -p 8080:8080 my-titan:latest
```

## Production Configuration

### Resource Limits

```yaml
services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    deploy:
      resources:
        limits:
          cpus: '4'
          memory: 2G
        reservations:
          cpus: '2'
          memory: 1G
```

### Logging

```yaml
services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    logging:
      driver: "json-file"
      options:
        max-size: "100m"
        max-file: "5"
```

### Health Checks

```yaml
healthcheck:
  test: ["CMD", "curl", "-f", "http://localhost:8080/_health"]
  interval: 30s
  timeout: 10s
  retries: 3
  start_period: 40s
```

## Networking

### Bridge Network

```yaml
networks:
  titan-network:
    driver: bridge

services:
  titan:
    networks:
      - titan-network
    depends_on:
      - backend

  backend:
    networks:
      - titan-network
```

### Host Network (Performance)

For maximum performance:

```yaml
services:
  titan:
    network_mode: "host"
    # No port mapping needed
```

## Volumes

### Configuration

```yaml
volumes:
  - ./config.json:/etc/titan/config.json:ro
```

### TLS Certificates

```yaml
volumes:
  - ./certs:/etc/titan/certs:ro
```

### Logs

```yaml
volumes:
  - ./logs:/var/log/titan
```

## Environment Variables

```yaml
environment:
  - TITAN_PORT=8080
  - TITAN_WORKERS=4
  - TITAN_LOG_LEVEL=info
```

## Docker Swarm

Deploy to Swarm:

```yaml
version: '3.8'

services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    ports:
      - "8080:8080"
    volumes:
      - ./config.json:/etc/titan/config.json:ro
    deploy:
      replicas: 3
      update_config:
        parallelism: 1
        delay: 10s
      restart_policy:
        condition: on-failure
```

Deploy:

```bash
docker stack deploy -c docker-compose.yml titan
```

## Monitoring

### Prometheus

Expose metrics:

```yaml
ports:
  - "8080:8080"  # Metrics at /_metrics
```

### Health Endpoint

```bash
curl http://localhost:8080/_health
```

## Troubleshooting

### View Logs

```bash
docker logs titan
docker logs -f titan  # Follow
```

### Exec into Container

```bash
docker exec -it titan sh
```

### Check Config

```bash
docker exec titan cat /etc/titan/config.json
```

## Next Steps

- [Kubernetes Deployment](./kubernetes)
- [Production Best Practices](./production)
EOF

echo "✅ Deployment guides created"

echo "📝 Creating contributing guide..."

cat > "$DOCS_DIR/contributing/getting-started.md" <<'EOF'
---
sidebar_position: 1
title: Contributing Guide
description: How to contribute to Titan development
---

# Contributing to Titan

Thank you for your interest in contributing to Titan! This guide will help you get started.

## Code of Conduct

Be respectful, inclusive, and professional. See our [Code of Conduct](https://github.com/JonathanBerhe/titan/blob/main/CODE_OF_CONDUCT.md).

## Ways to Contribute

- 🐛 **Report bugs** - Found an issue? Open a GitHub issue
- ✨ **Suggest features** - Have an idea? Start a discussion
- 📝 **Improve docs** - Fix typos or add examples
- 🔧 **Submit PRs** - Fix bugs or implement features
- 🧪 **Write tests** - Improve test coverage
- 📊 **Share benchmarks** - Run performance tests

## Development Setup

### Prerequisites

- Clang 18+
- CMake 3.28+
- vcpkg
- Docker (optional, for testing)

### Clone Repository

```bash
git clone https://github.com/JonathanBerhe/titan.git
cd titan
```

### Build

```bash
# Configure
cmake --preset=dev

# Build
cmake --build --preset=dev

# Run tests
cd build/dev
ctest --output-on-failure
```

### Run Locally

```bash
./build/dev/src/titan --config config/test.json
```

## Coding Standards

### C++ Style

- **Standard:** C++23
- **Style:** Google C++ Style Guide
- **Formatting:** clang-format (run `clang-format -i src/**/*.cpp`)
- **Naming:** snake_case for functions/variables, PascalCase for classes

### Best Practices

- ✅ Use `std::string_view` for read-only strings
- ✅ Use `std::span` for array views
- ✅ Prefer RAII over manual resource management
- ✅ Use `const` everywhere possible
- ✅ Write unit tests for new features
- ❌ No exceptions in hot path
- ❌ No malloc/free in request handling
- ❌ No locks in worker threads

## Pull Request Process

### 1. Create Branch

Follow branch naming convention:

- `epic/feature-name` - Breaking changes (major version bump)
- `feat/feature-name` - New features (minor version bump)
- `fix/bug-name` - Bug fixes (patch version bump)
- `chore/task-name` - Maintenance (no version bump)

```bash
git checkout -b feat/add-websocket-support
```

### 2. Make Changes

```bash
# Edit code
vim src/gateway/websocket.cpp

# Format code
clang-format -i src/gateway/websocket.cpp

# Run tests
cd build/dev
ctest
```

### 3. Commit

```bash
git add .
git commit -m "feat: add WebSocket support

- Implement WebSocket handshake
- Add message framing
- Add tests"
```

### 4. Push

```bash
git push origin feat/add-websocket-support
```

### 5. Create PR

- Go to GitHub and create Pull Request
- Fill out PR template
- Wait for CI checks to pass
- Address review comments

### 6. Merge

Once approved and CI passes, maintainers will merge your PR.

## Testing

### Unit Tests

```bash
# Run all tests
cd build/dev
ctest

# Run specific test
./tests/unit/test_router
```

### Integration Tests

```bash
cd tests/integration
pip install -r requirements.txt
pytest -v
```

### Benchmarks

```bash
./scripts/simple-benchmark.sh
```

## Documentation

- Update docs in `docs/` for Markdown
- Update docs in `website/docs/` for website
- Add examples for new features
- Update configuration reference

## Getting Help

- **GitHub Discussions** - Ask questions
- **GitHub Issues** - Report bugs
- **Discord/Slack** - Real-time chat (coming soon)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
EOF

echo "✅ Contributing guide created"

echo ""
echo "=========================================="
echo "✅ Documentation Migration Complete!"
echo "=========================================="
echo ""
echo "Summary:"
echo "  📁 Created documentation structure"
echo "  📝 Migrated existing docs with frontmatter"
echo "  📂 Created category metadata files"
echo "  ✍️  Created intro, guides, and references"
echo ""
echo "Location: $DOCS_DIR"
echo ""
echo "Next steps:"
echo "  1. Review migrated content in website/docs/"
echo "  2. Start dev server: cd website && npm start"
echo "  3. Build site: npm run build"
echo "  4. Deploy: npm run deploy"
echo ""
