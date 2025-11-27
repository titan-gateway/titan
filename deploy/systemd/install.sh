#!/usr/bin/env bash
#
# Titan API Gateway - systemd installation script
# This script installs Titan as a systemd service on Linux
#

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration
BINARY_PATH="${BINARY_PATH:-./build/static/src/titan}"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
CONFIG_DIR="${CONFIG_DIR:-/etc/titan}"
DATA_DIR="${DATA_DIR:-/var/lib/titan}"
LOG_DIR="${LOG_DIR:-/var/log/titan}"
SERVICE_FILE="titan.service"
USER="titan"
GROUP="titan"

# Helper functions
info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root (use sudo)"
fi

# Check if systemd is available
if ! command -v systemctl &> /dev/null; then
    error "systemd is not available on this system"
fi

# Check if binary exists
if [[ ! -f "$BINARY_PATH" ]]; then
    error "Binary not found at $BINARY_PATH. Build the project first or set BINARY_PATH environment variable."
fi

info "Starting Titan installation..."

# Create user and group
if ! id "$USER" &>/dev/null; then
    info "Creating user $USER..."
    useradd --system --no-create-home --shell /bin/false "$USER"
else
    info "User $USER already exists"
fi

# Create directories
info "Creating directories..."
mkdir -p "$INSTALL_DIR"
mkdir -p "$CONFIG_DIR"
mkdir -p "$DATA_DIR"
mkdir -p "$LOG_DIR"

# Copy binary
info "Installing binary to $INSTALL_DIR..."
cp "$BINARY_PATH" "$INSTALL_DIR/titan"
chmod 755 "$INSTALL_DIR/titan"

# Copy sample config if not exists
if [[ ! -f "$CONFIG_DIR/config.json" ]]; then
    if [[ -f "config/benchmark.json" ]]; then
        info "Installing sample configuration..."
        cp config/benchmark.json "$CONFIG_DIR/config.json"
        warn "Sample configuration installed. Please edit $CONFIG_DIR/config.json before starting the service."
    else
        warn "No sample configuration found. You must create $CONFIG_DIR/config.json manually."
    fi
fi

# Set ownership
info "Setting ownership..."
chown -R "$USER:$GROUP" "$CONFIG_DIR"
chown -R "$USER:$GROUP" "$DATA_DIR"
chown -R "$USER:$GROUP" "$LOG_DIR"

# Install systemd service
info "Installing systemd service..."
cp "deploy/systemd/$SERVICE_FILE" "/etc/systemd/system/$SERVICE_FILE"
chmod 644 "/etc/systemd/system/$SERVICE_FILE"

# Reload systemd
info "Reloading systemd..."
systemctl daemon-reload

# Enable service
info "Enabling service..."
systemctl enable titan.service

info ""
info "Installation complete!"
info ""
info "Next steps:"
info "  1. Edit configuration: $CONFIG_DIR/config.json"
info "  2. Start service: systemctl start titan"
info "  3. Check status: systemctl status titan"
info "  4. View logs: journalctl -u titan -f"
info ""
info "Useful commands:"
info "  Start:   sudo systemctl start titan"
info "  Stop:    sudo systemctl stop titan"
info "  Restart: sudo systemctl restart titan"
info "  Status:  sudo systemctl status titan"
info "  Logs:    sudo journalctl -u titan -f"
info "  Enable:  sudo systemctl enable titan"
info "  Disable: sudo systemctl disable titan"
info ""
