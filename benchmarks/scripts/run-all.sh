#!/bin/bash
# Run complete benchmark suite - HTTP/1.1 + HTTP/2

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/utils.sh"

echo "========================================"
echo "Titan Complete Benchmark Suite"
echo "========================================"
echo ""
echo "This will run:"
echo "  1. HTTP/1.1 benchmarks (5 scenarios)"
echo "  2. HTTP/2 benchmarks (3 scenarios)"
echo "  3. Result comparison"
echo ""
echo "Estimated time: ~30 minutes"
echo ""

read -p "Continue? [Y/n] " -n 1 -r
echo
if [[ $REPLY =~ ^[Nn]$ ]]; then
    exit 0
fi

# Clean up old results (optional)
read -p "Clean old results? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    log_info "Cleaning old results..."
    rm -f "$BENCHMARK_DIR/results"/*.json
    log_success "Old results cleaned"
fi

# Run HTTP/1.1 benchmarks
echo ""
log_info "Starting HTTP/1.1 benchmarks..."
"$SCRIPT_DIR/run-http1.sh" all

# Run HTTP/2 benchmarks
echo ""
log_info "Starting HTTP/2 benchmarks..."
"$SCRIPT_DIR/run-http2.sh" all

# Compare results
echo ""
echo "========================================"
echo "Generating Comparison Report"
echo "========================================"

# HTTP/1.1 comparison
echo ""
log_info "HTTP/1.1 Results:"
"$SCRIPT_DIR/compare.py" "$BENCHMARK_DIR/results"/http1-*.json

# HTTP/2 comparison
echo ""
log_info "HTTP/2 Results:"
"$SCRIPT_DIR/compare.py" "$BENCHMARK_DIR/results"/http2-*.json

# Overall summary
echo ""
log_info "Overall Results:"
"$SCRIPT_DIR/compare.py" "$BENCHMARK_DIR/results"/*.json --summary

echo ""
echo "========================================"
echo "Benchmark Suite Complete!"
echo "========================================"
echo ""
echo "Results saved to: $BENCHMARK_DIR/results/"
echo ""
echo "To re-run specific tests:"
echo "  HTTP/1.1: ./scripts/run-http1.sh [small|medium|large]"
echo "  HTTP/2:   ./scripts/run-http2.sh [small|medium]"
echo ""
