# Titan API Gateway - systemd Deployment

This directory contains systemd service files for deploying Titan on bare metal Linux servers.

## Prerequisites

- Linux with systemd (Ubuntu 20.04+, RHEL 8+, Debian 11+)
- Static binary built (`build/static/src/titan`)
- Root/sudo access

## Quick Installation

```bash
# Build static binary first
cmake --preset=static
cmake --build --preset=static

# Install with default paths
sudo ./deploy/systemd/install.sh
```

## Custom Installation

Override default paths using environment variables:

```bash
# Custom installation paths
sudo BINARY_PATH=/path/to/titan \
     INSTALL_DIR=/opt/titan/bin \
     CONFIG_DIR=/opt/titan/etc \
     DATA_DIR=/opt/titan/data \
     LOG_DIR=/opt/titan/logs \
     ./deploy/systemd/install.sh
```

## Manual Installation

If you prefer to install manually:

### 1. Create User

```bash
sudo useradd --system --no-create-home --shell /bin/false titan
```

### 2. Create Directories

```bash
sudo mkdir -p /usr/local/bin
sudo mkdir -p /etc/titan
sudo mkdir -p /var/lib/titan
sudo mkdir -p /var/log/titan
```

### 3. Install Binary

```bash
sudo cp build/static/src/titan /usr/local/bin/
sudo chmod 755 /usr/local/bin/titan
```

### 4. Create Configuration

```bash
sudo cp config/benchmark.json /etc/titan/config.json
sudo nano /etc/titan/config.json  # Edit as needed
```

### 5. Set Ownership

```bash
sudo chown -R titan:titan /etc/titan
sudo chown -R titan:titan /var/lib/titan
sudo chown -R titan:titan /var/log/titan
```

### 6. Install Service

```bash
sudo cp deploy/systemd/titan.service /etc/systemd/system/
sudo chmod 644 /etc/systemd/system/titan.service
sudo systemctl daemon-reload
```

### 7. Enable and Start

```bash
sudo systemctl enable titan
sudo systemctl start titan
```

## Configuration

### Service File Location

`/etc/systemd/system/titan.service`

### Configuration Options

Edit `/etc/systemd/system/titan.service` to customize:

```ini
[Service]
# Binary path
ExecStart=/usr/local/bin/titan --config /etc/titan/config.json

# User/Group
User=titan
Group=titan

# Resource limits
LimitNOFILE=1048576
LimitNPROC=512
LimitMEMLOCK=infinity

# Environment variables
Environment="TITAN_LOG_LEVEL=info"
Environment="TITAN_WORKERS=4"
```

After editing, reload:

```bash
sudo systemctl daemon-reload
sudo systemctl restart titan
```

## Service Management

### Start Service

```bash
sudo systemctl start titan
```

### Stop Service

```bash
sudo systemctl stop titan
```

### Restart Service

```bash
sudo systemctl restart titan
```

### Check Status

```bash
sudo systemctl status titan
```

### Enable on Boot

```bash
sudo systemctl enable titan
```

### Disable on Boot

```bash
sudo systemctl disable titan
```

## Logging

### View Logs

```bash
# Follow logs in real-time
sudo journalctl -u titan -f

# View last 100 lines
sudo journalctl -u titan -n 100

# View logs since boot
sudo journalctl -u titan -b

# View logs for specific time range
sudo journalctl -u titan --since "2024-01-01 00:00:00" --until "2024-01-01 23:59:59"

# View logs with priority level
sudo journalctl -u titan -p err

# Export logs to file
sudo journalctl -u titan > titan-logs.txt
```

### Log Rotation

Logs are managed by journald. Configure retention:

```bash
# Edit journald config
sudo nano /etc/systemd/journald.conf

# Set max disk usage (example: 1GB)
SystemMaxUse=1G

# Set retention time (example: 7 days)
MaxRetentionSec=7d

# Restart journald
sudo systemctl restart systemd-journald
```

## Monitoring

### Check Service Health

```bash
# Service status
systemctl is-active titan

# Health endpoint
curl http://localhost:8080/_health
```

### Resource Usage

```bash
# CPU and memory usage
systemctl status titan

# Detailed resource stats
systemd-cgtop

# Process info
ps aux | grep titan
```

### File Descriptors

```bash
# Check current limit
cat /proc/$(pidof titan)/limits | grep "open files"

# Check actual usage
lsof -p $(pidof titan) | wc -l
```

## Troubleshooting

### Service Won't Start

```bash
# Check status and errors
sudo systemctl status titan

# View detailed logs
sudo journalctl -u titan -n 50 --no-pager

# Check binary permissions
ls -la /usr/local/bin/titan

# Check config syntax
/usr/local/bin/titan --config /etc/titan/config.json --validate

# Test binary manually
sudo -u titan /usr/local/bin/titan --config /etc/titan/config.json
```

### Permission Denied Errors

```bash
# Check file ownership
ls -la /etc/titan/
ls -la /var/lib/titan/
ls -la /var/log/titan/

# Fix ownership
sudo chown -R titan:titan /etc/titan
sudo chown -R titan:titan /var/lib/titan
sudo chown -R titan:titan /var/log/titan
```

### Port Binding Issues

```bash
# Check if port is already in use
sudo lsof -i :8080
sudo lsof -i :8443

# Verify capability
sudo getcap /usr/local/bin/titan

# Add capability if missing
sudo setcap cap_net_bind_service=+ep /usr/local/bin/titan
```

### High Memory Usage

```bash
# Check memory usage
systemctl status titan

# Set memory limit
sudo systemctl edit titan

# Add:
[Service]
MemoryMax=1G
MemoryHigh=800M

# Restart
sudo systemctl restart titan
```

## Performance Tuning

### System Limits

Add to `/etc/security/limits.conf`:

```
titan soft nofile 1048576
titan hard nofile 1048576
titan soft memlock unlimited
titan hard memlock unlimited
```

### Kernel Parameters

Add to `/etc/sysctl.conf`:

```
# Network tuning
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.core.netdev_max_backlog = 65535
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_fin_timeout = 30
net.ipv4.tcp_keepalive_time = 300
net.ipv4.tcp_tw_reuse = 1

# File descriptors
fs.file-max = 2097152

# Memory
vm.swappiness = 10
```

Apply changes:

```bash
sudo sysctl -p
```

### CPU Affinity

Pin Titan to specific CPU cores:

```bash
sudo systemctl edit titan

# Add:
[Service]
CPUAffinity=0-3
```

## Uninstallation

### Automated Uninstall

```bash
sudo ./deploy/systemd/uninstall.sh
```

### Manual Uninstall

```bash
# Stop and disable service
sudo systemctl stop titan
sudo systemctl disable titan

# Remove service file
sudo rm /etc/systemd/system/titan.service
sudo systemctl daemon-reload

# Remove binary
sudo rm /usr/local/bin/titan

# Remove data (optional)
sudo rm -rf /etc/titan
sudo rm -rf /var/lib/titan
sudo rm -rf /var/log/titan

# Remove user (optional)
sudo userdel titan
```

## Security Hardening

The service file includes security hardening features:

- **Non-root execution**: Runs as `titan` user
- **Minimal capabilities**: Only `CAP_NET_BIND_SERVICE`
- **Filesystem protection**: Read-only root, private `/tmp`
- **Syscall filtering**: Restricted to safe syscalls
- **Resource limits**: Bounded file descriptors and memory

### Additional Hardening

#### SELinux Policy

On RHEL/CentOS with SELinux:

```bash
# Create SELinux policy
sudo semanage fcontext -a -t bin_t "/usr/local/bin/titan"
sudo restorecon -v /usr/local/bin/titan

# Allow network binding
sudo semanage port -a -t http_port_t -p tcp 8080
sudo semanage port -a -t http_port_t -p tcp 8443
```

#### AppArmor Profile

On Ubuntu with AppArmor:

```bash
# Create profile at /etc/apparmor.d/usr.local.bin.titan
# See AppArmor documentation for details
```

#### Firewall

```bash
# Allow HTTP/HTTPS
sudo ufw allow 8080/tcp
sudo ufw allow 8443/tcp

# Or using firewalld
sudo firewall-cmd --permanent --add-port=8080/tcp
sudo firewall-cmd --permanent --add-port=8443/tcp
sudo firewall-cmd --reload
```

## Backup and Restore

### Backup Configuration

```bash
# Backup config
sudo cp /etc/titan/config.json /backup/titan-config-$(date +%Y%m%d).json

# Backup service file
sudo cp /etc/systemd/system/titan.service /backup/
```

### Restore Configuration

```bash
# Restore config
sudo cp /backup/titan-config-20240101.json /etc/titan/config.json

# Reload service
sudo systemctl restart titan
```

## Integration with Monitoring

### Prometheus Node Exporter

```bash
# Install node_exporter to expose system metrics
# Titan's /_metrics endpoint can be scraped by Prometheus
```

### Telegraf

```bash
# Configure Telegraf to collect systemd metrics
[[inputs.systemd_units]]
  pattern = "titan.service"
```

## Support

For issues:
- Check logs: `sudo journalctl -u titan -f`
- Verify config: `/usr/local/bin/titan --config /etc/titan/config.json --validate`
- GitHub Issues: https://github.com/JonathanBerhe/titan/issues
