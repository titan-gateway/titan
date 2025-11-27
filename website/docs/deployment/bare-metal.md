---
sidebar_position: 4
title: Bare Metal Deployment
---

# Bare Metal Deployment

Deploy Titan directly on Linux servers with systemd for maximum performance and minimal overhead.

## Architecture

Bare metal deployment uses **Scenario 1: Single Titan Instance**:

```
                    ┌─────────────┐
                    │   Client    │
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    Titan    │
                    │   (systemd) │
                    │  Built-in   │
                    │Load Balancer│
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
         ┌────▼────┐  ┌────▼────┐ ┌────▼────┐
         │Backend 1│  │Backend 2│ │Backend 3│
         └─────────┘  └─────────┘  └─────────┘
```

**Features:**
- **Native performance**: No container overhead
- **systemd integration**: Standard Linux service management
- **Low resource footprint**: Minimal dependencies
- **Direct hardware access**: Maximum CPU and network efficiency

## Prerequisites

Before deploying on bare metal, ensure your system has:

- **Operating System**: Linux (Ubuntu 22.04+, Debian 12+, RHEL 9+, or compatible)
- **CPU**: x86-64 or ARM64 with SSE4.2/NEON support
- **RAM**: Minimum 512MB, recommended 2GB+
- **Disk**: 100MB for binary, 1GB+ for logs
- **Permissions**: sudo/root access for installation
- **Network**: Port 8080 (HTTP) and 8443 (HTTPS) available

## Installation Methods

### Option 1: Automated Installation (Recommended)

The automated installer builds, installs, and configures Titan with systemd:

```bash
# Clone repository
git clone https://github.com/JonathanBerhe/titan.git
cd titan

# Build static binary
cmake --preset=static
cmake --build --preset=static

# Install (creates systemd service)
sudo ./deploy/systemd/install.sh
```

**What the installer does:**
1. Copies binary to `/usr/local/bin/titan`
2. Creates dedicated `titan` user
3. Creates `/etc/titan/` directory for configuration
4. Installs systemd service unit
5. Enables service to start on boot
6. Sets required capabilities (`CAP_NET_BIND_SERVICE`)

### Option 2: Manual Installation

For full control over the installation process:

#### Step 1: Build Titan

```bash
# Clone and build
git clone https://github.com/JonathanBerhe/titan.git
cd titan

# Static build (no runtime dependencies)
cmake --preset=static
cmake --build --preset=static

# Verify binary
./build/static/src/titan --version
```

#### Step 2: Install Binary

```bash
# Copy to system path
sudo cp build/static/src/titan /usr/local/bin/titan
sudo chmod +x /usr/local/bin/titan

# Verify installation
which titan
titan --version
```

#### Step 3: Create User

```bash
# Create dedicated system user
sudo useradd --system --no-create-home --shell /bin/false titan
```

#### Step 4: Setup Configuration

```bash
# Create directories
sudo mkdir -p /etc/titan
sudo mkdir -p /var/log/titan

# Create configuration file
sudo tee /etc/titan/config.json > /dev/null <<EOF
{
  "server": {
    "port": 8080,
    "workers": 4,
    "tls": {
      "enabled": false
    }
  },
  "upstreams": [
    {
      "name": "backend",
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
      "upstream": "backend"
    }
  ]
}
EOF

# Set ownership
sudo chown -R titan:titan /etc/titan
sudo chown -R titan:titan /var/log/titan
```

#### Step 5: Create systemd Service

```bash
sudo tee /etc/systemd/system/titan.service > /dev/null <<EOF
[Unit]
Description=Titan API Gateway
Documentation=https://github.com/JonathanBerhe/titan
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=titan
Group=titan

# Binary and configuration
ExecStart=/usr/local/bin/titan --config /etc/titan/config.json

# Restart policy
Restart=on-failure
RestartSec=5s

# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

# Security
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/log/titan

# Required capabilities
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=titan

[Install]
WantedBy=multi-user.target
EOF
```

#### Step 6: Enable and Start Service

```bash
# Reload systemd
sudo systemctl daemon-reload

# Enable service (start on boot)
sudo systemctl enable titan

# Start service
sudo systemctl start titan

# Check status
sudo systemctl status titan
```

## Service Management

### Basic Commands

```bash
# Start Titan
sudo systemctl start titan

# Stop Titan
sudo systemctl stop titan

# Restart Titan
sudo systemctl restart titan

# Check status
sudo systemctl status titan

# View logs (real-time)
sudo journalctl -u titan -f

# View logs (last 100 lines)
sudo journalctl -u titan -n 100

# View logs (since boot)
sudo journalctl -u titan -b
```

### Hot Configuration Reload

Reload configuration without downtime:

```bash
# Edit configuration
sudo vi /etc/titan/config.json

# Reload (sends SIGHUP)
sudo systemctl reload titan

# Or manually
sudo kill -HUP $(pidof titan)

# Verify reload succeeded
sudo journalctl -u titan -n 20
```

## Configuration

### Production Configuration Example

```json
{
  "server": {
    "port": 80,
    "workers": 8,
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
      "load_balancing": "least_connections",
      "backends": [
        { "host": "10.0.1.10", "port": 8080 },
        { "host": "10.0.1.11", "port": 8080 },
        { "host": "10.0.1.12", "port": 8080 }
      ]
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
    "allowed_origins": ["https://app.example.com"],
    "allowed_methods": ["GET", "POST", "PUT", "DELETE"],
    "max_age": 3600
  },
  "rate_limit": {
    "requests_per_second": 100,
    "burst": 200
  }
}
```

### TLS Certificates

#### Self-Signed (Development)

```bash
# Generate self-signed certificate
sudo openssl req -x509 -newkey rsa:4096 \
  -keyout /etc/titan/certs/server.key \
  -out /etc/titan/certs/server.crt \
  -days 365 -nodes \
  -subj "/CN=localhost"

# Set permissions
sudo chown titan:titan /etc/titan/certs/*
sudo chmod 600 /etc/titan/certs/server.key
```

#### Let's Encrypt (Production)

```bash
# Install certbot
sudo apt install certbot

# Obtain certificate
sudo certbot certonly --standalone -d api.example.com

# Link certificates
sudo ln -s /etc/letsencrypt/live/api.example.com/fullchain.pem /etc/titan/certs/server.crt
sudo ln -s /etc/letsencrypt/live/api.example.com/privkey.pem /etc/titan/certs/server.key

# Auto-renewal (add to crontab)
0 0 * * * certbot renew --quiet && systemctl reload titan
```

## Security

### Required Capabilities

Titan needs `CAP_NET_BIND_SERVICE` to bind to privileged ports (80, 443):

```bash
# Set capability on binary
sudo setcap cap_net_bind_service=+ep /usr/local/bin/titan

# Verify
getcap /usr/local/bin/titan
# Output: /usr/local/bin/titan = cap_net_bind_service+ep
```

### Security Hardening

**Run as non-root user** (already configured in systemd service):

```ini
[Service]
User=titan
Group=titan
```

**Filesystem protection:**

```ini
[Service]
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/log/titan
PrivateTmp=true
```

**Capability restrictions:**

```ini
[Service]
NoNewPrivileges=true
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
```

### Firewall Configuration

**UFW (Ubuntu/Debian):**

```bash
# Allow HTTP/HTTPS
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp

# Enable firewall
sudo ufw enable

# Check status
sudo ufw status
```

**firewalld (RHEL/CentOS):**

```bash
# Allow HTTP/HTTPS
sudo firewall-cmd --permanent --add-service=http
sudo firewall-cmd --permanent --add-service=https
sudo firewall-cmd --reload

# Check status
sudo firewall-cmd --list-all
```

## Performance Tuning

### System Limits

#### File Descriptors

Increase open file limits for high-concurrency scenarios:

```bash
# Edit systemd service
sudo systemctl edit titan

# Add override:
[Service]
LimitNOFILE=65536
LimitNPROC=4096

# Reload
sudo systemctl daemon-reload
sudo systemctl restart titan
```

**Verify limits:**

```bash
# Check current limits
cat /proc/$(pidof titan)/limits

# Should show:
# Max open files    65536    65536    files
```

#### Kernel Parameters

Optimize TCP/IP stack for high throughput:

```bash
# Edit sysctl configuration
sudo tee -a /etc/sysctl.conf > /dev/null <<EOF
# TCP socket buffer sizes
net.core.rmem_max = 268435456
net.core.wmem_max = 268435456
net.ipv4.tcp_rmem = 4096 87380 134217728
net.ipv4.tcp_wmem = 4096 65536 134217728

# Connection tracking
net.netfilter.nf_conntrack_max = 1048576
net.nf_conntrack_max = 1048576

# TCP performance
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 15
net.ipv4.tcp_max_syn_backlog = 4096
net.core.somaxconn = 4096

# Enable TCP Fast Open
net.ipv4.tcp_fastopen = 3
EOF

# Apply changes
sudo sysctl -p
```

### Worker Configuration

Set workers to match CPU core count:

```json
{
  "server": {
    "workers": 8  // Set to: nproc (Linux) or sysctl -n hw.ncpu (macOS)
  }
}
```

**Find optimal worker count:**

```bash
# Linux
nproc

# Or manually count CPU cores
lscpu | grep "^CPU(s):"
```

## Monitoring

### Health Checks

Titan exposes health endpoints:

```bash
# Check if Titan is responding
curl http://localhost:8080/health

# Expected response: {"status":"ok"}
```

### Systemd Status

Monitor service health with systemd:

```bash
# Service status
sudo systemctl status titan

# Check for failures
sudo systemctl is-failed titan

# View recent journal entries
sudo journalctl -u titan -n 50 --no-pager
```

### Logs

**View real-time logs:**

```bash
# Follow logs
sudo journalctl -u titan -f

# Filter by priority
sudo journalctl -u titan -p err -f

# Filter by time
sudo journalctl -u titan --since "1 hour ago"
sudo journalctl -u titan --since "2025-01-01" --until "2025-01-02"
```

**Log rotation (via journald):**

```bash
# Configure journal size limits
sudo mkdir -p /etc/systemd/journald.conf.d/
sudo tee /etc/systemd/journald.conf.d/titan.conf > /dev/null <<EOF
[Journal]
SystemMaxUse=500M
SystemMaxFileSize=100M
MaxRetentionSec=1week
EOF

# Restart journald
sudo systemctl restart systemd-journald
```

### Resource Monitoring

**CPU and memory usage:**

```bash
# Using systemctl
systemctl status titan | grep -E "Memory|CPU"

# Using top
top -p $(pidof titan)

# Using htop (install if needed: sudo apt install htop)
htop -p $(pidof titan)
```

## Troubleshooting

### Port Already in Use

```bash
# Check what's using the port
sudo lsof -i :8080

# Or
sudo netstat -tulpn | grep :8080

# Kill conflicting process
sudo kill -9 <PID>

# Or change Titan's port in config.json
```

### Permission Denied

```bash
# Check if capability is set
getcap /usr/local/bin/titan

# If not, set it
sudo setcap cap_net_bind_service=+ep /usr/local/bin/titan

# Or run on non-privileged port (>1024)
# Edit /etc/titan/config.json and set port to 8080
```

### Service Won't Start

```bash
# Check service status
sudo systemctl status titan

# View detailed logs
sudo journalctl -u titan -n 100 --no-pager

# Check configuration syntax
titan --config /etc/titan/config.json --validate

# Test binary manually
sudo -u titan /usr/local/bin/titan --config /etc/titan/config.json
```

### High Memory Usage

```bash
# Check current memory usage
ps aux | grep titan

# Adjust worker count (reduce if memory constrained)
# Edit /etc/titan/config.json:
# "workers": 2  // Lower value uses less memory

# Restart service
sudo systemctl restart titan
```

### Backend Connection Issues

```bash
# Test backend connectivity
curl http://backend-host:backend-port/

# Check DNS resolution
nslookup backend-host

# Check firewall rules
sudo iptables -L -n | grep <backend-port>

# View Titan's connection errors
sudo journalctl -u titan | grep -i "connection refused\|timeout"
```

## Production Checklist

- [ ] Static binary built and tested
- [ ] Dedicated `titan` user created
- [ ] systemd service installed and enabled
- [ ] Configuration file validated and optimized
- [ ] TLS certificates installed and auto-renewal configured
- [ ] Firewall rules configured (ports 80, 443)
- [ ] System limits increased (file descriptors, kernel params)
- [ ] Worker count set to CPU core count
- [ ] Health check endpoint accessible
- [ ] Log rotation configured
- [ ] Monitoring alerts configured
- [ ] Backup strategy for configuration
- [ ] Disaster recovery plan tested

## Next Steps

- **[Docker Deployment](./docker)** - Alternative containerized deployment
- **[Kubernetes Deployment](./kubernetes)** - Production HA deployment
- **[Configuration Reference](../configuration/overview)** - Detailed configuration options
- **[Architecture Overview](../architecture/overview)** - Understand Titan's design
