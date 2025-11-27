#!/usr/bin/env bash
#
# Titan API Gateway - systemd uninstallation script
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
CONFIG_DIR="${CONFIG_DIR:-/etc/titan}"
DATA_DIR="${DATA_DIR:-/var/lib/titan}"
LOG_DIR="${LOG_DIR:-/var/log/titan}"
SERVICE_FILE="titan.service"
USER="titan"

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

info "Starting Titan uninstallation..."

# Stop and disable service
if systemctl is-active --quiet titan; then
    info "Stopping service..."
    systemctl stop titan
fi

if systemctl is-enabled --quiet titan 2>/dev/null; then
    info "Disabling service..."
    systemctl disable titan
fi

# Remove service file
if [[ -f "/etc/systemd/system/$SERVICE_FILE" ]]; then
    info "Removing service file..."
    rm -f "/etc/systemd/system/$SERVICE_FILE"
    systemctl daemon-reload
fi

# Remove binary
if [[ -f "$INSTALL_DIR/titan" ]]; then
    info "Removing binary..."
    rm -f "$INSTALL_DIR/titan"
fi

# Ask about config and data
read -p "Remove configuration directory $CONFIG_DIR? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    info "Removing configuration..."
    rm -rf "$CONFIG_DIR"
fi

read -p "Remove data directory $DATA_DIR? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    info "Removing data..."
    rm -rf "$DATA_DIR"
fi

read -p "Remove log directory $LOG_DIR? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    info "Removing logs..."
    rm -rf "$LOG_DIR"
fi

read -p "Remove user $USER? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if id "$USER" &>/dev/null; then
        info "Removing user..."
        userdel "$USER"
    fi
fi

info ""
info "Uninstallation complete!"
