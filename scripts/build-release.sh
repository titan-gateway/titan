#!/usr/bin/env bash
#
# Titan API Gateway - Local Release Builder
# Builds all CPU variants for local testing or manual release
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
OUTPUT_DIR="${OUTPUT_DIR:-./dist}"
PARALLEL_JOBS="${PARALLEL_JOBS:-$(nproc)}"

# Build variants
X86_VARIANTS=("generic" "haswell" "skylake" "zen3")
ARM_VARIANTS=("generic-arm" "neoverse-n1" "neoverse-v1" "apple-m1")

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

section() {
    echo ""
    echo -e "${BLUE}===================================================${NC}"
    echo -e "${BLUE} $1${NC}"
    echo -e "${BLUE}===================================================${NC}"
}

# Detect current architecture
detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)
            echo "x86_64"
            ;;
        aarch64|arm64)
            echo "arm64"
            ;;
        *)
            error "Unsupported architecture: $(uname -m)"
            ;;
    esac
}

# Build single variant
build_variant() {
    local variant="$1"
    local preset="release-$variant"

    section "Building variant: $variant"

    info "Configuring..."
    if ! cmake --preset="$preset"; then
        error "Configuration failed for $variant"
    fi

    info "Building with $PARALLEL_JOBS parallel jobs..."
    if ! cmake --build --preset="$preset" --parallel "$PARALLEL_JOBS"; then
        error "Build failed for $variant"
    fi

    info "Stripping binary..."
    strip "build/$preset/src/titan"

    # Get binary info
    local size
    size=$(ls -lh "build/$preset/src/titan" | awk '{print $5}')
    info "Binary size: $size"

    # Create tarball
    local arch
    if [[ "$variant" == *"arm"* ]] || [[ "$variant" == *"neoverse"* ]] || [[ "$variant" == *"apple"* ]]; then
        arch="arm64"
    else
        arch="x86_64"
    fi

    mkdir -p "$OUTPUT_DIR"
    local tarball="$OUTPUT_DIR/titan-$variant-linux-$arch.tar.gz"

    info "Creating tarball: $tarball"
    tar -czf "$tarball" -C "build/$preset/src" titan

    # Create checksum
    info "Creating checksum..."
    (cd "$OUTPUT_DIR" && sha256sum "titan-$variant-linux-$arch.tar.gz" > "titan-$variant-linux-$arch.tar.gz.sha256")

    info "✓ Build complete: $tarball"
}

# Build all variants for current architecture
build_all() {
    local arch
    arch=$(detect_arch)

    section "Building all variants for $arch"

    local variants
    if [[ "$arch" == "x86_64" ]]; then
        variants=("${X86_VARIANTS[@]}")
    else
        variants=("${ARM_VARIANTS[@]}")
    fi

    local total=${#variants[@]}
    local current=0

    for variant in "${variants[@]}"; do
        current=$((current + 1))
        info "[$current/$total] Building $variant..."
        if ! build_variant "$variant"; then
            warn "Failed to build $variant, continuing..."
            continue
        fi
    done

    section "Build Summary"
    info "All builds complete! Output directory: $OUTPUT_DIR"
    ls -lh "$OUTPUT_DIR"
}

# Clean build directories
clean() {
    section "Cleaning build directories"

    local variants=("${X86_VARIANTS[@]}" "${ARM_VARIANTS[@]}")

    for variant in "${variants[@]}"; do
        local preset="release-$variant"
        if [[ -d "build/$preset" ]]; then
            info "Removing build/$preset..."
            rm -rf "build/$preset"
        fi
    done

    if [[ -d "$OUTPUT_DIR" ]]; then
        info "Removing $OUTPUT_DIR..."
        rm -rf "$OUTPUT_DIR"
    fi

    info "Clean complete!"
}

# Show usage
usage() {
    cat <<EOF
Titan Release Builder

Usage: $0 [COMMAND] [OPTIONS]

Commands:
  all             Build all variants for current architecture (default)
  variant NAME    Build specific variant
  clean           Clean all build directories
  list            List available variants
  help            Show this help

Options:
  OUTPUT_DIR      Output directory for tarballs (default: ./dist)
  PARALLEL_JOBS   Number of parallel build jobs (default: $(nproc))

Examples:
  # Build all variants for current architecture
  $0 all

  # Build specific variant
  $0 variant neoverse-v1

  # Build with custom output directory
  OUTPUT_DIR=/tmp/release $0 all

  # Clean everything
  $0 clean

  # Build with more parallel jobs
  PARALLEL_JOBS=8 $0 all

Available Variants:
  x86-64:
    - generic         (Intel 2009+, AMD 2011+)
    - haswell         (Intel 2013+, AMD 2017+)
    - skylake         (Intel 2015+, AMD 2019+)
    - zen3            (AMD Zen 3)

  ARM64:
    - generic-arm     (ARMv8+)
    - neoverse-n1     (AWS Graviton 2)
    - neoverse-v1     (AWS Graviton 3/4)
    - apple-m1        (Apple Silicon)

EOF
}

# List variants
list_variants() {
    echo "x86-64 variants:"
    for v in "${X86_VARIANTS[@]}"; do
        echo "  - $v"
    done

    echo ""
    echo "ARM64 variants:"
    for v in "${ARM_VARIANTS[@]}"; do
        echo "  - $v"
    done
}

# Main
main() {
    local command="${1:-all}"

    case "$command" in
        all)
            build_all
            ;;
        variant)
            if [[ -z "${2:-}" ]]; then
                error "Variant name required. Use: $0 variant NAME"
            fi
            build_variant "$2"
            ;;
        clean)
            clean
            ;;
        list)
            list_variants
            ;;
        help|--help|-h)
            usage
            ;;
        *)
            error "Unknown command: $command (use 'help' for usage)"
            ;;
    esac
}

main "$@"
