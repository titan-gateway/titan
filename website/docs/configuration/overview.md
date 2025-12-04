---
sidebar_position: 1
title: Configuration Overview
---

# Configuration Overview

Titan uses JSON configuration with hot-reload support.

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
      "name": "api",
      "load_balancing": "round_robin",
      "backends": [{ "host": "10.0.1.10", "port": 8080, "weight": 5 }]
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "upstream": "api",
      "middleware": ["cors", "rate_limit"]
    }
  ],
  "cors": {
    "allowed_origins": ["*"],
    "allowed_methods": ["GET", "POST"]
  },
  "rate_limit": {
    "requests_per_second": 100,
    "burst": 200
  }
}
```

## Configuration Examples by Use Case

### Example 1: Basic Reverse Proxy

The simplest configuration—proxy all traffic to a single backend:

```json
{
  "server": {
    "port": 8080,
    "workers": 4
  },
  "upstreams": [
    {
      "name": "backend",
      "backends": [
        { "host": "localhost", "port": 3000 }
      ]
    }
  ],
  "routes": [
    { "path": "/*", "upstream": "backend" }
  ]
}
```

**Use case**: Development environments, single-service deployments, or proxying to a containerized app.

### Example 2: Load Balancing Across Multiple Backends

Distribute traffic across multiple instances with weighted round-robin:

```json
{
  "server": {
    "port": 8080,
    "workers": 4
  },
  "upstreams": [
    {
      "name": "api",
      "load_balancing": "weighted_round_robin",
      "backends": [
        { "host": "10.0.1.10", "port": 8080, "weight": 5 },
        { "host": "10.0.1.11", "port": 8080, "weight": 3 },
        { "host": "10.0.1.12", "port": 8080, "weight": 2 }
      ]
    }
  ],
  "routes": [
    { "path": "/api/*", "upstream": "api" }
  ]
}
```

**Use case**: Horizontal scaling with backends of different capacities. The first server receives 50% of traffic, the second 30%, and the third 20%.

### Example 3: TLS Termination with Multiple Routes

Handle HTTPS at the edge and route to different backends:

```json
{
  "server": {
    "port": 443,
    "workers": 4,
    "tls": {
      "enabled": true,
      "cert_file": "/etc/titan/certs/server.crt",
      "key_file": "/etc/titan/certs/server.key",
      "protocols": ["TLSv1.2", "TLSv1.3"]
    }
  },
  "upstreams": [
    {
      "name": "api",
      "backends": [
        { "host": "api-backend", "port": 8080 }
      ]
    },
    {
      "name": "static",
      "backends": [
        { "host": "static-backend", "port": 9000 }
      ]
    }
  ],
  "routes": [
    { "path": "/api/*", "upstream": "api" },
    { "path": "/assets/*", "upstream": "static" },
    { "path": "/*", "upstream": "static" }
  ]
}
```

**Use case**: Microservices architecture where Titan terminates TLS and routes to internal HTTP services based on path.

### Example 4: Rate Limiting per Route

Apply different rate limits to different endpoints:

```json
{
  "server": { "port": 8080, "workers": 4 },
  "upstreams": [
    {
      "name": "api",
      "backends": [{ "host": "api-backend", "port": 8080 }]
    }
  ],
  "routes": [
    {
      "path": "/api/public/*",
      "upstream": "api",
      "middleware": ["public_rate_limit"]
    },
    {
      "path": "/api/premium/*",
      "upstream": "api",
      "middleware": ["premium_rate_limit"]
    }
  ],
  "rate_limits": {
    "public_rate_limit": {
      "requests_per_second": 10,
      "burst": 20
    },
    "premium_rate_limit": {
      "requests_per_second": 100,
      "burst": 200
    }
  }
}
```

**Use case**: API tiering where premium users get higher rate limits than public users.

### Example 5: JWT Authentication + CORS

Chain middleware for cross-origin requests with JWT authentication:

```json
{
  "server": { "port": 8080, "workers": 4 },
  "upstreams": [
    {
      "name": "api",
      "backends": [{ "host": "api-backend", "port": 8080 }]
    }
  ],
  "routes": [
    {
      "path": "/api/*",
      "upstream": "api"
    }
  ],
  "jwt": {
    "enabled": true,
    "keys": [
      {
        "algorithm": "RS256",
        "public_key_path": "/etc/titan/keys/jwt_public.pem"
      }
    ],
    "allowed_issuers": ["https://auth.example.com"]
  },
  "cors": {
    "allowed_origins": ["https://app.example.com", "https://dashboard.example.com"],
    "allowed_methods": ["GET", "POST", "PUT", "DELETE"],
    "allowed_headers": ["Authorization", "Content-Type"],
    "max_age": 3600
  },
  "rate_limit": {
    "enabled": true,
    "requests_per_second": 100,
    "burst": 200
  }
}
```

**Use case**: SPA frontend calling backend API—CORS headers allow browser requests, JWT validates tokens, rate limiting prevents abuse.

## Configuration Field Reference

### Server

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `port` | number | **required** | Port to listen on |
| `workers` | number | CPU count | Number of worker threads (recommend 1 per core) |
| `host` | string | `0.0.0.0` | Bind address |
| `tls.enabled` | boolean | `false` | Enable TLS/HTTPS |
| `tls.cert_file` | string | - | Path to certificate file (PEM format) |
| `tls.key_file` | string | - | Path to private key file (PEM format) |
| `tls.protocols` | array | `["TLSv1.2", "TLSv1.3"]` | Allowed TLS versions |

### Upstreams

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | **required** | Unique upstream identifier |
| `load_balancing` | string | `round_robin` | Strategy: `round_robin`, `weighted_round_robin`, `least_connections`, `random` |
| `backends[].host` | string | **required** | Backend hostname or IP |
| `backends[].port` | number | **required** | Backend port |
| `backends[].weight` | number | `1` | Weight for weighted strategies (higher = more traffic) |

### Routes

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `path` | string | **required** | URL path pattern (supports `*` wildcard) |
| `upstream` | string | **required** | Upstream name to proxy to |
| `middleware` | array | `[]` | Ordered list of middleware to apply |

### Middleware: CORS

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `allowed_origins` | array | `["*"]` | Allowed origins (`*` for all, or specific domains) |
| `allowed_methods` | array | `["GET", "POST"]` | Allowed HTTP methods |
| `allowed_headers` | array | `[]` | Allowed request headers |
| `max_age` | number | `0` | Preflight cache duration (seconds) |

### Middleware: Rate Limiting

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `requests_per_second` | number | **required** | Maximum requests per second per client IP |
| `burst` | number | `requests_per_second * 2` | Burst capacity (token bucket size) |

### Middleware: JWT Authentication

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | `false` | Enable JWT authentication |
| `keys[]` | array | `[]` | Verification keys (RS256/ES256/HS256) |
| `allowed_issuers` | array | `[]` | Whitelist of valid token issuers |
| `allowed_audiences` | array | `[]` | Whitelist of valid token audiences |

See **[JWT Authentication](./jwt-authentication.md)** for complete configuration details.

## Load Balancing Strategies

**`round_robin`**: Distributes requests evenly across backends in order. Simple and effective for homogeneous backends.

**`weighted_round_robin`**: Respects backend `weight` values. Use when backends have different capacities (e.g., different instance sizes).

**`least_connections`**: Routes to the backend with fewest active connections. Ideal for long-lived connections or variable request durations.

**`random`**: Randomly selects a backend. Statistically similar to round-robin but avoids tracking state.

## Hot Reload

Titan supports **zero-downtime configuration reloads** using RCU (Read-Copy-Update):

```bash
# Send reload signal
kill -HUP $(pidof titan)

# Or with systemd
systemctl reload titan
```

### How It Works

1. **Validate**: New configuration is parsed and validated
2. **Build**: New routing tree and upstreams are constructed
3. **Atomic Swap**: Configuration pointer is atomically replaced
4. **Graceful Cleanup**: Old configuration is freed when all in-flight requests complete

**Key Benefits:**
- Zero dropped requests during reload
- Instant failover if validation fails
- In-flight requests continue with old config
- New requests immediately use new config

## Next Steps

- **[Architecture Overview](../architecture/overview.md)** - Learn about Titan's design
- **[Quick Start Guide](../getting-started/quickstart.md)** - Try configuration examples
- **[Deployment Guide](../deployment/overview.md)** - Deploy with your config
