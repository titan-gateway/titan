#!/usr/bin/env bash
# Generate CPU flamegraph using perf and FlameGraph
# Usage: ./generate_flamegraph.sh [duration] [connections] [threads]

set -e

# Parameters
DURATION=${1:-30}
CONNECTIONS=${2:-100}
THREADS=${3:-4}

# Directories
PROFILE_DIR="profiling"
FLAMEGRAPH_DIR="tools/flamegraph"
BUILD_DIR="build/release"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo -e "${RED}✗ FlameGraph generation requires Linux and perf${NC}"
    echo -e "${YELLOW}  On macOS, use Instruments instead:${NC}"
    echo -e "${YELLOW}    xcode-select --install${NC}"
    echo -e "${YELLOW}    instruments -t 'Time Profiler' -D profiling/instruments.trace ./build/release/src/titan --config config/benchmark-http1.json${NC}"
    exit 1
fi

# Check for perf
if ! command -v perf &>/dev/null; then
    echo -e "${RED}✗ perf not found${NC}"
    echo -e "${YELLOW}  Install: sudo apt-get install linux-tools-common linux-tools-generic${NC}"
    exit 1
fi

# Check for FlameGraph
if [ ! -d "$FLAMEGRAPH_DIR" ]; then
    echo -e "${YELLOW}⚠ FlameGraph not found, cloning...${NC}"
    mkdir -p tools
    git clone https://github.com/brendangregg/FlameGraph.git "$FLAMEGRAPH_DIR"
fi

# Build Titan with frame pointers for better stack traces
echo -e "${CYAN}→ Building Titan with frame pointers...${NC}"
cmake --preset=release -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
cmake --build --preset=release --parallel $(nproc)

# Create profile directory
mkdir -p "$PROFILE_DIR"

# Start Titan in background
echo -e "${CYAN}→ Starting Titan...${NC}"
$BUILD_DIR/src/titan --config config/benchmark-http1.json &
TITAN_PID=$!
echo -e "${GREEN}✓ Titan started (PID: $TITAN_PID)${NC}"

# Wait for Titan to be ready
sleep 2

# Start perf recording
echo -e "${CYAN}→ Recording CPU profile for ${DURATION}s...${NC}"
perf record -F 99 -g -p $TITAN_PID -o $PROFILE_DIR/perf.data -- sleep $DURATION &
PERF_PID=$!

# Generate load
echo -e "${CYAN}→ Generating load...${NC}"
wrk -t$THREADS -c$CONNECTIONS -d${DURATION}s http://localhost:8080/api >/dev/null 2>&1 || true

# Wait for perf to finish
wait $PERF_PID 2>/dev/null || true

# Stop Titan
echo -e "${CYAN}→ Stopping Titan...${NC}"
kill $TITAN_PID 2>/dev/null || true
wait $TITAN_PID 2>/dev/null || true

# Generate flamegraph
echo -e "${CYAN}→ Generating flamegraph...${NC}"
perf script -i $PROFILE_DIR/perf.data | \
    $FLAMEGRAPH_DIR/stackcollapse-perf.pl | \
    $FLAMEGRAPH_DIR/flamegraph.pl --title "Titan CPU Profile" \
    > $PROFILE_DIR/flamegraph.svg

# Also generate a text report
echo -e "${CYAN}→ Generating text report...${NC}"
perf report -i $PROFILE_DIR/perf.data --stdio --no-children -n | head -100 > $PROFILE_DIR/perf_report.txt

echo -e "${GREEN}✅ Flamegraph generated successfully!${NC}"
echo -e "${GREEN}   SVG: $PROFILE_DIR/flamegraph.svg${NC}"
echo -e "${GREEN}   Report: $PROFILE_DIR/perf_report.txt${NC}"
echo -e ""
echo -e "Top 10 functions by CPU time:"
perf report -i $PROFILE_DIR/perf.data --stdio --no-children -n | grep -A 15 "^#" | tail -15
