#!/usr/bin/env bash
#
# Titan API Gateway - CPU-Optimized Installer
# Auto-detects CPU and downloads/installs the most optimized binary
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
GITHUB_REPO="JonathanBerhe/titan"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
VERSION="${VERSION:-latest}"

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

debug() {
    if [[ "${DEBUG:-0}" == "1" ]]; then
        echo -e "${BLUE}[DEBUG]${NC} $1"
    fi
}

# Detect CPU architecture
detect_arch() {
    local arch
    arch=$(uname -m)

    case "$arch" in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "arm64"
            ;;
        *)
            error "Unsupported architecture: $arch"
            ;;
    esac
}

# Detect specific x86-64 CPU variant
detect_x86_variant() {
    local cpu_flags
    cpu_flags=$(grep -m1 '^flags' /proc/cpuinfo | cut -d: -f2)

    debug "CPU flags: $cpu_flags"

    # Check for AMD Zen 3 (EPYC Milan, Ryzen 5000)
    if grep -q 'AMD' /proc/cpuinfo; then
        local cpu_family
        cpu_family=$(grep -m1 'cpu family' /proc/cpuinfo | awk '{print $NF}')

        if [[ "$cpu_family" == "25" ]]; then
            # Zen 3 (family 25)
            echo "zen3"
            return
        fi
    fi

    # Check for Intel/AMD by features (newest to oldest)
    if echo "$cpu_flags" | grep -q 'avx512f'; then
        # Skylake-X or newer (2017+)
        echo "skylake"
        return
    fi

    if echo "$cpu_flags" | grep -q 'avx2'; then
        # Haswell or newer (2013+)
        echo "haswell"
        return
    fi

    # Fallback to generic x86-64-v2 (2009+)
    echo "generic"
}

# Detect specific ARM64 CPU variant
detect_arm_variant() {
    local cpu_model
    cpu_model=$(grep -m1 'CPU implementer' /proc/cpuinfo | awk '{print $NF}')
    local cpu_part
    cpu_part=$(grep -m1 'CPU part' /proc/cpuinfo | awk '{print $NF}')

    debug "CPU implementer: $cpu_model, CPU part: $cpu_part"

    # ARM implementer codes
    case "$cpu_model" in
        0x41)  # ARM
            case "$cpu_part" in
                0xd40)  # Neoverse V1 (Graviton 3/4)
                    echo "neoverse-v1"
                    return
                    ;;
                0xd0c)  # Neoverse N1 (Graviton 2)
                    echo "neoverse-n1"
                    return
                    ;;
            esac
            ;;
        0x61)  # Apple
            # Apple M1/M2/M3 (all use similar microarchitecture)
            echo "apple-m1"
            return
            ;;
    esac

    # Check for specific features if model detection failed
    local cpu_features
    cpu_features=$(grep -m1 '^Features' /proc/cpuinfo | cut -d: -f2 || echo "")

    if echo "$cpu_features" | grep -q 'sve'; then
        # SVE support = Neoverse V1 or newer
        echo "neoverse-v1"
        return
    fi

    if echo "$cpu_features" | grep -q 'dotprod'; then
        # DotProd support = at least Neoverse N1
        echo "neoverse-n1"
        return
    fi

    # Fallback to generic ARM64
    echo "generic-arm"
}

# Detect optimal binary variant
detect_variant() {
    local arch
    arch=$(detect_arch)

    info "Detected architecture: $arch"

    if [[ "$arch" == "x86_64" ]]; then
        detect_x86_variant
    else
        detect_arm_variant
    fi
}

# Get download URL for variant
get_download_url() {
    local variant="$1"
    local arch

    if [[ "$variant" == *"arm"* ]] || [[ "$variant" == *"neoverse"* ]] || [[ "$variant" == *"apple"* ]]; then
        arch="arm64"
    else
        arch="x86_64"
    fi

    if [[ "$VERSION" == "latest" ]]; then
        echo "https://github.com/$GITHUB_REPO/releases/latest/download/titan-${variant}-linux-${arch}.tar.gz"
    else
        echo "https://github.com/$GITHUB_REPO/releases/download/v${VERSION}/titan-${variant}-linux-${arch}.tar.gz"
    fi
}

# Download and install binary
install_binary() {
    local variant="$1"
    local url
    url=$(get_download_url "$variant")

    info "Downloading Titan ($variant) from: $url"

    local tmpdir
    tmpdir=$(mktemp -d)
    trap "rm -rf $tmpdir" EXIT

    if ! curl -fsSL "$url" -o "$tmpdir/titan.tar.gz"; then
        warn "Failed to download optimized binary for $variant"
        warn "Falling back to generic build..."

        # Fallback logic
        if [[ "$variant" == *"arm"* ]] || [[ "$variant" == *"neoverse"* ]] || [[ "$variant" == *"apple"* ]]; then
            url=$(get_download_url "generic-arm")
        else
            url=$(get_download_url "generic")
        fi

        info "Downloading generic build from: $url"
        curl -fsSL "$url" -o "$tmpdir/titan.tar.gz" || error "Failed to download generic binary"
    fi

    info "Extracting..."
    tar -xzf "$tmpdir/titan.tar.gz" -C "$tmpdir"

    info "Installing to $INSTALL_DIR..."
    sudo install -m 755 "$tmpdir/titan" "$INSTALL_DIR/titan"

    info "Installation complete!"
}

# Verify installation
verify_installation() {
    if [[ ! -x "$INSTALL_DIR/titan" ]]; then
        error "Binary not found at $INSTALL_DIR/titan"
    fi

    info "Verifying binary..."
    "$INSTALL_DIR/titan" --version || warn "Version check failed (binary may not support --version yet)"

    info "Binary installed successfully at: $INSTALL_DIR/titan"
}

# Show CPU info
show_cpu_info() {
    info "CPU Information:"

    if [[ -f /proc/cpuinfo ]]; then
        local cpu_name
        cpu_name=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs)

        if [[ -z "$cpu_name" ]]; then
            # ARM doesn't have "model name", try other fields
            cpu_name=$(grep -m1 'CPU part' /proc/cpuinfo | cut -d: -f2 | xargs)
        fi

        echo "  Model: $cpu_name"
    fi

    local arch
    arch=$(detect_arch)
    echo "  Architecture: $arch"

    if [[ "$arch" == "x86_64" ]]; then
        local flags
        flags=$(grep -m1 '^flags' /proc/cpuinfo | cut -d: -f2)
        echo -n "  Features: "
        echo "$flags" | grep -o '\(avx512f\|avx2\|avx\|sse4_2\)' | tr '\n' ' '
        echo
    else
        local features
        features=$(grep -m1 '^Features' /proc/cpuinfo | cut -d: -f2 || echo "")
        echo -n "  Features: "
        echo "$features" | grep -o '\(sve\|dotprod\|fp16\|crypto\)' | tr '\n' ' '
        echo
    fi
}

# Print usage
usage() {
    cat <<EOF
Titan API Gateway - CPU-Optimized Installer

Usage: $0 [OPTIONS]

Options:
  --help              Show this help message
  --version VERSION   Install specific version (default: latest)
  --variant VARIANT   Force specific variant (skip auto-detection)
  --show-cpu          Show CPU information and recommended variant
  --dry-run           Show what would be installed without installing

Environment Variables:
  INSTALL_DIR         Installation directory (default: /usr/local/bin)
  VERSION             Version to install (default: latest)
  DEBUG               Enable debug output (DEBUG=1)

Supported Variants:
  x86-64:
    - generic         Generic x86-64-v2 (Intel 2009+, AMD 2011+)
    - haswell         Intel Haswell (2013+), AMD Excavator (2017+)
    - skylake         Intel Skylake (2015+), AMD Zen 2 (2019+)
    - zen3            AMD Zen 3 (EPYC Milan, Ryzen 5000)

  ARM64:
    - generic-arm     Generic ARMv8-A (all ARM64)
    - neoverse-n1     AWS Graviton 2, Azure Cobalt
    - neoverse-v1     AWS Graviton 3/4 (SVE support)
    - apple-m1        Apple Silicon M1/M2/M3

Examples:
  # Auto-detect and install
  $0

  # Show recommended variant for your CPU
  $0 --show-cpu

  # Install specific version
  $0 --version 0.1.0

  # Force specific variant
  $0 --variant neoverse-v1

  # Dry run
  $0 --dry-run

EOF
}

# Main
main() {
    local variant=""
    local show_cpu=0
    local dry_run=0

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)
                usage
                exit 0
                ;;
            --version)
                VERSION="$2"
                shift 2
                ;;
            --variant)
                variant="$2"
                shift 2
                ;;
            --show-cpu)
                show_cpu=1
                shift
                ;;
            --dry-run)
                dry_run=1
                shift
                ;;
            *)
                error "Unknown option: $1 (use --help for usage)"
                ;;
        esac
    done

    # Show CPU info mode
    if [[ $show_cpu -eq 1 ]]; then
        show_cpu_info
        local recommended
        recommended=$(detect_variant)
        info "Recommended variant: $recommended"
        exit 0
    fi

    # Auto-detect if not specified
    if [[ -z "$variant" ]]; then
        variant=$(detect_variant)
        info "Auto-detected CPU variant: $variant"
    else
        info "Using forced variant: $variant"
    fi

    # Dry run mode
    if [[ $dry_run -eq 1 ]]; then
        local url
        url=$(get_download_url "$variant")
        info "Would download from: $url"
        info "Would install to: $INSTALL_DIR/titan"
        exit 0
    fi

    # Check root for installation
    if [[ ! -w "$INSTALL_DIR" ]] && [[ $EUID -ne 0 ]]; then
        warn "Installation directory not writable. You may need sudo."
    fi

    # Install
    install_binary "$variant"
    verify_installation

    info ""
    info "Titan has been installed successfully!"
    info "Run: titan --config /path/to/config.json"
}

main "$@"
