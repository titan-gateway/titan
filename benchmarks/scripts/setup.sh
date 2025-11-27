#!/bin/bash
# Setup script - Install all proxies for benchmarking
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_DIR="$(dirname "$SCRIPT_DIR")"
WORKSPACE_DIR="$(dirname "$BENCHMARK_DIR")"

echo "========================================="
echo "Titan Benchmark Suite - Setup"
echo "========================================="
echo ""
echo "This will install:"
echo "  - Nginx (reverse proxy)"
echo "  - HAProxy (load balancer)"
echo "  - Envoy (cloud-native proxy)"
echo "  - wrk (HTTP/1.1 benchmark tool)"
echo "  - h2load (HTTP/2 benchmark tool)"
echo "  - Python dependencies (comparison tools)"
echo ""
echo "Note: Kong installation is optional (slow)"
echo ""

# Check if we're in container
if [ ! -f /.dockerenv ]; then
    echo "WARNING: Not running in container. Recommended to run in titan-dev."
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Update package list
echo "Updating package list..."
apt-get update -qq

# Install common dependencies
echo ""
echo "[1/7] Installing common dependencies..."
apt-get install -y -qq \
    curl \
    wget \
    git \
    build-essential \
    libssl-dev \
    libpcre3-dev \
    zlib1g-dev \
    liblua5.1-0-dev \
    software-properties-common

# Install Nginx
echo ""
echo "[2/7] Installing Nginx..."
if command -v nginx &> /dev/null; then
    echo "  ✓ Nginx already installed ($(nginx -v 2>&1 | grep -oP '\d+\.\d+\.\d+'))"
else
    apt-get install -y -qq nginx
    echo "  ✓ Nginx installed ($(nginx -v 2>&1 | grep -oP '\d+\.\d+\.\d+'))"
fi

# Install HAProxy
echo ""
echo "[3/7] Installing HAProxy..."
if command -v haproxy &> /dev/null; then
    echo "  ✓ HAProxy already installed ($(haproxy -v | head -1 | grep -oP '\d+\.\d+\.\d+'))"
else
    apt-get install -y -qq haproxy
    echo "  ✓ HAProxy installed ($(haproxy -v | head -1 | grep -oP '\d+\.\d+\.\d+'))"
fi

# Install Envoy
echo ""
echo "[4/7] Installing Envoy..."
if command -v envoy &> /dev/null; then
    echo "  ✓ Envoy already installed"
else
    echo "  Downloading Envoy binary..."
    ENVOY_VERSION="1.28.0"
    ARCH="$(uname -m)"

    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
        ENVOY_URL="https://github.com/envoyproxy/envoy/releases/download/v${ENVOY_VERSION}/envoy-${ENVOY_VERSION}-linux-aarch_64"
    else
        ENVOY_URL="https://github.com/envoyproxy/envoy/releases/download/v${ENVOY_VERSION}/envoy-${ENVOY_VERSION}-linux-x86_64"
    fi

    wget -q "$ENVOY_URL" -O /usr/local/bin/envoy
    chmod +x /usr/local/bin/envoy
    echo "  ✓ Envoy installed ($ENVOY_VERSION)"
fi

# Install wrk (HTTP/1.1 benchmarking)
echo ""
echo "[5/7] Installing wrk..."
if command -v wrk &> /dev/null; then
    echo "  ✓ wrk already installed"
else
    apt-get install -y -qq wrk
    echo "  ✓ wrk installed"
fi

# Install h2load (HTTP/2 benchmarking)
echo ""
echo "[6/7] Installing h2load (nghttp2)..."
if command -v h2load &> /dev/null; then
    echo "  ✓ h2load already installed"
else
    apt-get install -y -qq nghttp2-client
    echo "  ✓ h2load installed"
fi

# Install Python dependencies for comparison scripts
echo ""
echo "[7/7] Installing Python dependencies..."
pip3 install -q tabulate matplotlib psutil 2>/dev/null || true
echo "  ✓ Python dependencies installed"

# Create results directory
mkdir -p "$BENCHMARK_DIR/results"
echo ""
echo "  ✓ Created results directory"

# Create HAProxy combined cert file (needed for SSL)
echo ""
echo "Preparing SSL certificates..."
if [ -f "$WORKSPACE_DIR/certs/server-cert.pem" ] && [ -f "$WORKSPACE_DIR/certs/server-key.pem" ]; then
    cat "$WORKSPACE_DIR/certs/server-cert.pem" "$WORKSPACE_DIR/certs/server-key.pem" > "$WORKSPACE_DIR/certs/server.pem"
    echo "  ✓ Combined cert created for HAProxy"
else
    echo "  ⚠ SSL certificates not found in /workspace/certs/"
    echo "    HTTP/2 benchmarks will not work without them."
fi

# Optional: Install Kong (very slow, skip by default)
echo ""
read -p "Install Kong? (slow, optional) [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Installing Kong..."
    if command -v kong &> /dev/null; then
        echo "  ✓ Kong already installed"
    else
        # Kong requires specific Ubuntu version and database
        echo "  Note: Kong installation is complex and may fail"
        echo "  Skipping Kong for now. Benchmarks will run without it."
    fi
else
    echo "  ⊘ Skipping Kong installation"
fi

# Verify installations
echo ""
echo "========================================="
echo "Verification"
echo "========================================="
echo "Nginx:   $(which nginx 2>/dev/null && echo '✓' || echo '✗')"
echo "HAProxy: $(which haproxy 2>/dev/null && echo '✓' || echo '✗')"
echo "Envoy:   $(which envoy 2>/dev/null && echo '✓' || echo '✗')"
echo "wrk:     $(which wrk 2>/dev/null && echo '✓' || echo '✗')"
echo "h2load:  $(which h2load 2>/dev/null && echo '✓' || echo '✗')"
echo ""

# Test backend server
echo "Testing mock backend..."
cd "$WORKSPACE_DIR/tests/mock-backend"
if pgrep -f "uvicorn main:app" > /dev/null; then
    echo "  ✓ Backend already running"
else
    python3 main.py > /dev/null 2>&1 &
    sleep 2
    if curl -s http://localhost:3001/small > /dev/null; then
        echo "  ✓ Backend started and responding"
    else
        echo "  ✗ Backend failed to start"
        exit 1
    fi
fi

echo ""
echo "========================================="
echo "Setup Complete!"
echo "========================================="
echo ""
echo "Next steps:"
echo "  1. Run HTTP/1.1 benchmarks: ./scripts/run-http1.sh"
echo "  2. Run HTTP/2 benchmarks:   ./scripts/run-http2.sh"
echo "  3. Compare results:         ./scripts/compare.py results/*.json"
echo ""
