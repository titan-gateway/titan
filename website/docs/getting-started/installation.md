---
sidebar_position: 1
title: Installation
---

# Installation

Install Titan on your system using one of the following methods.

## Binary Installation (Recommended)

```bash
curl -fsSL https://raw.githubusercontent.com/JonathanBerhe/titan/main/scripts/install-optimized.sh | bash
```

This auto-detects your CPU and installs the optimal binary variant.

## Docker

```bash
docker pull ghcr.io/JonathanBerhe/titan:latest

docker run -d -p 8080:8080 \
  -v $(pwd)/config.json:/etc/titan/config.json \
  ghcr.io/JonathanBerhe/titan:latest --config /etc/titan/config.json
```

## Kubernetes (Helm)

```bash
helm install titan oci://ghcr.io/JonathanBerhe/charts/titan \
  --version 0.1.0 \
  --namespace titan \
  --create-namespace
```

## Next Steps

- **[Quick Start Guide](./quickstart.md)** - Create your first proxy in 5 minutes
- **[Building from Source](./building-from-source.md)** - Compile Titan yourself
- **[Configuration Reference](../configuration/overview.md)** - Explore all configuration options
- **[Deployment Guide](../deployment/overview.md)** - Deploy to production
