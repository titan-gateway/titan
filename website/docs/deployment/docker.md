---
sidebar_position: 2
title: Docker Deployment
---

# Docker Deployment

## Docker Compose

```yaml
version: '3.8'

services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    ports:
      - "8080:8080"
    volumes:
      - ./config.json:/etc/titan/config.json:ro
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/_health"]
      interval: 10s
```

## Production Configuration

```yaml
services:
  titan:
    image: ghcr.io/JonathanBerhe/titan:latest
    deploy:
      resources:
        limits:
          cpus: '4'
          memory: 2G
```

## Next Steps

- See [Deployment Overview](./overview) for other deployment methods
