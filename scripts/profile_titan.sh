#!/bin/bash
set -euo pipefail

# profile_titan.sh - Comprehensive profiling suite for Titan API Gateway
# Profiles CPU, memory allocations, generates flame graphs, and validates hot path performance

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
TITAN_BIN="${TITAN_BIN:-./build/release/src/titan}"
CONFIG_FILE="${CONFIG_FILE:-config.json}"
PROFILE_DIR="${PROFILE_DIR:-./profile_results}"
DURATION="${DURATION:-30}"
BENCHMARK_URL="${BENCHMARK_URL:-http://localhost:8080/api/users/123}"
BENCHMARK_CONNECTIONS="${BENCHMARK_CONNECTIONS:-100}"
BENCHMARK_THREADS="${BENCHMARK_THREADS:-4}"

# Tool paths
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-/opt/FlameGraph}"

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_prerequisites() {
    log_info "Checking prerequisites..."

    local missing_tools=()

    # Check for Titan binary
    if [[ ! -f "$TITAN_BIN" ]]; then
        log_error "Titan binary not found at: $TITAN_BIN"
        log_info "Build with: cmake --preset=release && cmake --build --preset=release"
        exit 1
    fi

    # Check for config file
    if [[ ! -f "$CONFIG_FILE" ]]; then
        log_error "Config file not found at: $CONFIG_FILE"
        exit 1
    fi

    # Check for profiling tools
    command -v google-pprof >/dev/null 2>&1 || missing_tools+=("google-pprof (gperftools)")
    command -v heaptrack >/dev/null 2>&1 || missing_tools+=("heaptrack")
    command -v wrk >/dev/null 2>&1 || missing_tools+=("wrk")

    # Check for libprofiler (gperftools CPU profiler)
    if [[ ! -f /usr/lib/aarch64-linux-gnu/libprofiler.so ]] && [[ ! -f /usr/lib/x86_64-linux-gnu/libprofiler.so ]]; then
        missing_tools+=("libprofiler (gperftools)")
    fi

    # Check for FlameGraph scripts
    if [[ ! -d "$FLAMEGRAPH_DIR" ]]; then
        log_warning "FlameGraph directory not found at: $FLAMEGRAPH_DIR"
        log_info "Clone with: git clone https://github.com/brendangregg/FlameGraph /opt/FlameGraph"
        missing_tools+=("FlameGraph")
    fi

    # Check for objdump (for hot path analysis)
    command -v objdump >/dev/null 2>&1 || missing_tools+=("objdump")

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        log_error "Missing required tools: ${missing_tools[*]}"
        log_info "Install with:"
        echo "  apt-get install -y google-perftools libgoogle-perftools-dev heaptrack binutils"
        echo "  wget https://github.com/wg/wrk/archive/refs/tags/4.2.0.tar.gz"
        echo "  git clone https://github.com/brendangregg/FlameGraph /opt/FlameGraph"
        exit 1
    fi

    log_success "All prerequisites satisfied"
}

setup_environment() {
    log_info "Setting up profiling environment..."

    # Create output directory
    mkdir -p "$PROFILE_DIR"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    SESSION_DIR="$PROFILE_DIR/session_$TIMESTAMP"
    mkdir -p "$SESSION_DIR"

    log_success "Profile results will be saved to: $SESSION_DIR"
}

start_titan_server() {
    log_info "Starting Titan server..."

    # Kill any existing Titan binary processes (not the script)
    pkill -9 -f "^$TITAN_BIN" || true
    sleep 1

    # Start Titan in background
    "$TITAN_BIN" --config "$CONFIG_FILE" > "$SESSION_DIR/titan.log" 2>&1 &
    TITAN_PID=$!

    # Wait for server to be ready
    log_info "Waiting for server to be ready (PID: $TITAN_PID)..."
    for i in {1..30}; do
        if curl -s -o /dev/null -w "%{http_code}" "$BENCHMARK_URL" | grep -q "200\|404"; then
            log_success "Titan server is ready"
            return 0
        fi
        sleep 1
    done

    log_error "Titan server failed to start"
    cat "$SESSION_DIR/titan.log"
    exit 1
}

stop_titan_server() {
    log_info "Stopping Titan server (PID: $TITAN_PID)..."
    kill $TITAN_PID 2>/dev/null || true
    wait $TITAN_PID 2>/dev/null || true
    log_success "Titan server stopped"
}

run_cpu_profiling() {
    log_info "Running CPU profiling with gperftools (${DURATION}s)..."

    # Stop current server
    stop_titan_server

    # Find libprofiler.so
    LIBPROFILER=$(find /usr/lib -name "libprofiler.so" 2>/dev/null | head -1)
    if [[ -z "$LIBPROFILER" ]]; then
        log_error "libprofiler.so not found"
        return 1
    fi

    # Start Titan with CPU profiling enabled
    log_info "Starting Titan with CPU profiler..."
    CPUPROFILE="$SESSION_DIR/cpu.prof" \
    LD_PRELOAD="$LIBPROFILER" \
    "$TITAN_BIN" --config "$CONFIG_FILE" > "$SESSION_DIR/titan_cpu.log" 2>&1 &
    TITAN_PID=$!

    # Wait for server to be ready
    sleep 5
    for i in {1..30}; do
        if curl -s -o /dev/null -w "%{http_code}" "$BENCHMARK_URL" | grep -q "200\|404"; then
            break
        fi
        sleep 1
    done

    # Run benchmark
    log_info "Running benchmark workload..."
    wrk -t$BENCHMARK_THREADS -c$BENCHMARK_CONNECTIONS -d${DURATION}s "$BENCHMARK_URL" \
        > "$SESSION_DIR/wrk_cpu.txt" 2>&1

    # Stop server to flush profile data
    stop_titan_server

    # Generate pprof text report
    google-pprof --text "$TITAN_BIN" "$SESSION_DIR/cpu.prof" > "$SESSION_DIR/pprof_report.txt"

    # Generate flame graph
    if [[ -d "$FLAMEGRAPH_DIR" ]]; then
        log_info "Generating CPU flame graph..."
        google-pprof --collapsed "$TITAN_BIN" "$SESSION_DIR/cpu.prof" | \
            "$FLAMEGRAPH_DIR/flamegraph.pl" > "$SESSION_DIR/cpu_flamegraph.svg"
        log_success "CPU flame graph saved to: $SESSION_DIR/cpu_flamegraph.svg"
    fi

    log_success "CPU profiling complete"

    # Restart server for next tests
    start_titan_server
}

run_allocation_profiling() {
    log_info "Running allocation profiling with heaptrack..."

    # Stop current server
    stop_titan_server

    # Start Titan with heaptrack
    log_info "Starting Titan with heaptrack..."
    heaptrack -o "$SESSION_DIR/heaptrack" "$TITAN_BIN" --config "$CONFIG_FILE" \
        > "$SESSION_DIR/titan_heaptrack.log" 2>&1 &
    TITAN_PID=$!

    # Wait for server to be ready
    sleep 5
    for i in {1..30}; do
        if curl -s -o /dev/null -w "%{http_code}" "$BENCHMARK_URL" | grep -q "200\|404"; then
            break
        fi
        sleep 1
    done

    # Run benchmark
    log_info "Running benchmark workload..."
    wrk -t$BENCHMARK_THREADS -c$BENCHMARK_CONNECTIONS -d${DURATION}s "$BENCHMARK_URL" \
        > "$SESSION_DIR/wrk_heap.txt" 2>&1

    # Stop server to flush heaptrack data
    stop_titan_server

    # Find heaptrack output file
    HEAPTRACK_FILE=$(find "$SESSION_DIR" -name "heaptrack.titan.*.zst" | head -1)

    if [[ -n "$HEAPTRACK_FILE" ]]; then
        # Generate heaptrack report
        heaptrack_print "$HEAPTRACK_FILE" > "$SESSION_DIR/heaptrack_report.txt"

        # Generate allocation flame graph
        if [[ -d "$FLAMEGRAPH_DIR" ]]; then
            log_info "Generating allocation flame graph..."
            heaptrack_print "$HEAPTRACK_FILE" --print-flamegraph | \
                "$FLAMEGRAPH_DIR/flamegraph.pl" --title="Allocations" \
                > "$SESSION_DIR/allocation_flamegraph.svg"
            log_success "Allocation flame graph saved to: $SESSION_DIR/allocation_flamegraph.svg"
        fi

        log_success "Allocation profiling complete"
    else
        log_warning "Heaptrack output file not found"
    fi

    # Restart server for next tests
    start_titan_server
}

check_hot_path_allocations() {
    log_info "Checking for allocations in hot path..."

    # Extract hot functions from pprof report (skip header lines)
    HOT_FUNCTIONS=$(tail -n +6 "$SESSION_DIR/pprof_report.txt" | \
        head -20 | \
        awk '{print $NF}' | \
        grep -v "^Total" || true)

    # Check disassembly for malloc/calloc/free calls
    echo "Hot Path Allocation Analysis" > "$SESSION_DIR/hot_path_allocations.txt"
    echo "==============================" >> "$SESSION_DIR/hot_path_allocations.txt"
    echo "" >> "$SESSION_DIR/hot_path_allocations.txt"

    ALLOCATION_COUNT=0

    while IFS= read -r func; do
        if [[ -n "$func" ]]; then
            # Check if function contains allocation calls
            if objdump -d "$TITAN_BIN" 2>/dev/null | \
               grep -A 20 "<$func>:" | \
               grep -E "malloc|calloc|free|new|delete" > /dev/null 2>&1; then

                echo "WARNING: Function '$func' contains allocations" >> "$SESSION_DIR/hot_path_allocations.txt"
                ALLOCATION_COUNT=$((ALLOCATION_COUNT + 1))
            fi
        fi
    done <<< "$HOT_FUNCTIONS"

    if [[ $ALLOCATION_COUNT -eq 0 ]]; then
        echo "✓ No allocations detected in top 20 hot functions" >> "$SESSION_DIR/hot_path_allocations.txt"
        log_success "Hot path is allocation-free"
    else
        echo "" >> "$SESSION_DIR/hot_path_allocations.txt"
        echo "Found $ALLOCATION_COUNT hot functions with allocations!" >> "$SESSION_DIR/hot_path_allocations.txt"
        log_warning "Found $ALLOCATION_COUNT hot functions with allocations"
    fi
}

check_hot_path_locks() {
    log_info "Checking for locks in hot path..."

    echo "" >> "$SESSION_DIR/hot_path_allocations.txt"
    echo "Hot Path Lock Analysis" >> "$SESSION_DIR/hot_path_allocations.txt"
    echo "======================" >> "$SESSION_DIR/hot_path_allocations.txt"
    echo "" >> "$SESSION_DIR/hot_path_allocations.txt"

    # Extract hot functions from pprof report (skip header lines)
    HOT_FUNCTIONS=$(tail -n +6 "$SESSION_DIR/pprof_report.txt" | \
        head -20 | \
        awk '{print $NF}' | \
        grep -v "^Total" || true)

    LOCK_COUNT=0

    while IFS= read -r func; do
        if [[ -n "$func" ]]; then
            # Check if function contains lock operations
            if objdump -d "$TITAN_BIN" 2>/dev/null | \
               grep -A 20 "<$func>:" | \
               grep -E "mutex|pthread_mutex|lock|spinlock" > /dev/null 2>&1; then

                echo "WARNING: Function '$func' contains lock operations" >> "$SESSION_DIR/hot_path_allocations.txt"
                LOCK_COUNT=$((LOCK_COUNT + 1))
            fi
        fi
    done <<< "$HOT_FUNCTIONS"

    if [[ $LOCK_COUNT -eq 0 ]]; then
        echo "✓ No locks detected in top 20 hot functions" >> "$SESSION_DIR/hot_path_allocations.txt"
        log_success "Hot path is lock-free"
    else
        echo "" >> "$SESSION_DIR/hot_path_allocations.txt"
        echo "Found $LOCK_COUNT hot functions with locks!" >> "$SESSION_DIR/hot_path_allocations.txt"
        log_warning "Found $LOCK_COUNT hot functions with locks"
    fi
}

generate_summary_report() {
    log_info "Generating summary report..."

    REPORT="$SESSION_DIR/PROFILE_SUMMARY.md"

    cat > "$REPORT" <<EOF
# Titan Profiling Summary
**Generated:** $(date)
**Duration:** ${DURATION}s
**Binary:** $TITAN_BIN
**Config:** $CONFIG_FILE

---

## Benchmark Results

### CPU Profiling Workload
\`\`\`
$(cat "$SESSION_DIR/wrk_cpu.txt")
\`\`\`

### Allocation Profiling Workload
\`\`\`
$(cat "$SESSION_DIR/wrk_heap.txt")
\`\`\`

---

## CPU Profile (Top 30 Functions)

\`\`\`
$(head -40 "$SESSION_DIR/pprof_report.txt")
\`\`\`

---

## Allocation Profile

\`\`\`
$(head -50 "$SESSION_DIR/heaptrack_report.txt" 2>/dev/null || echo "Heaptrack report not available")
\`\`\`

---

## Hot Path Analysis

\`\`\`
$(cat "$SESSION_DIR/hot_path_allocations.txt")
\`\`\`

---

## Artifacts

- **CPU Flame Graph:** [cpu_flamegraph.svg](cpu_flamegraph.svg)
- **Allocation Flame Graph:** [allocation_flamegraph.svg](allocation_flamegraph.svg)
- **CPU Profile Data:** cpu.prof
- **pprof Report:** [pprof_report.txt](pprof_report.txt)
- **Heaptrack Report:** [heaptrack_report.txt](heaptrack_report.txt)
- **Hot Path Analysis:** [hot_path_allocations.txt](hot_path_allocations.txt)

---

## Recommendations

EOF

    # Add recommendations based on findings
    if grep -q "WARNING.*allocations" "$SESSION_DIR/hot_path_allocations.txt"; then
        echo "- ⚠️ **Hot path contains allocations** - Consider using arena allocators or object pools" >> "$REPORT"
    else
        echo "- ✓ Hot path is allocation-free" >> "$REPORT"
    fi

    if grep -q "WARNING.*locks" "$SESSION_DIR/hot_path_allocations.txt"; then
        echo "- ⚠️ **Hot path contains locks** - Consider thread-local storage or lock-free data structures" >> "$REPORT"
    else
        echo "- ✓ Hot path is lock-free" >> "$REPORT"
    fi

    # Check allocation rate
    if [[ -f "$SESSION_DIR/heaptrack_report.txt" ]]; then
        ALLOC_COUNT=$(grep -E "allocations:" "$SESSION_DIR/heaptrack_report.txt" | head -1 | awk '{print $1}' || echo "0")
        if [[ $ALLOC_COUNT -gt 1000000 ]]; then
            echo "- ⚠️ **High allocation rate** ($ALLOC_COUNT allocations) - Review mimalloc configuration" >> "$REPORT"
        fi
    fi

    log_success "Summary report saved to: $REPORT"

    # Print summary to console
    echo ""
    echo "=================================================="
    echo "  Profiling Complete"
    echo "=================================================="
    echo ""
    cat "$REPORT"
    echo ""
    echo "All results saved to: $SESSION_DIR"
}

cleanup() {
    log_info "Cleaning up..."
    stop_titan_server 2>/dev/null || true
}

main() {
    echo ""
    echo "=================================================="
    echo "  Titan Profiling Suite"
    echo "=================================================="
    echo ""

    trap cleanup EXIT

    check_prerequisites
    setup_environment
    start_titan_server

    run_cpu_profiling
    run_allocation_profiling

    check_hot_path_allocations
    check_hot_path_locks

    generate_summary_report

    log_success "Profiling suite completed successfully!"
}

main "$@"
