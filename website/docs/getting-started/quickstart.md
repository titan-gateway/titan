---
sidebar_position: 2
title: Quick Start  
---

# Quick Start

Create your first API gateway in 5 minutes.

## Step 1: Create Configuration

```json
{
  "server": {"port": 8080, "workers": 4},
  "upstreams": [{
    "name": "my_api",
    "load_balancing": "round_robin",
    "backends": [{"host": "localhost", "port": 3000}]
  }],
  "routes": [{"path": "/*", "upstream": "my_api"}]
}
```

## Step 2: Start Backend (Optional)

```bash
# Using Python
python3 -m http.server 3000

# Or Docker
docker run -p 3000:80 nginx:alpine
```

## Step 3: Start Titan

```bash
titan --config config.json
```

## Step 4: Test

```bash
curl http://localhost:8080/
curl http://localhost:8080/_health
```

## What's Next?

- [Configuration Reference](../configuration/overview)
- [Architecture](../architecture/overview)
- [Deployment](../deployment/docker)
