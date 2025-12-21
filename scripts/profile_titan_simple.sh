#!/bin/bash
set -euo pipefail

# profile_titan_simple.sh - Simplified profiling for Docker environments
# Focuses on benchmarking and basic allocation tracking without complex CPU profiling

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

TITAN_BIN="${TITAN_BIN:-./build/release/src/titan}"
CONFIG_FILE="${CONFIG_FILE:-config.json}"
PROFILE_DIR="${PROFILE_DIR:-./profile_results}"
DURATION="${DURATION:-10}"
BENCHMARK_URL="${BENCHMARK_URL:-http://localhost:8080/}"
BENCHMARK_CONNECTIONS="${BENCHMARK_CONNECTIONS:-100}"
BENCHMARK_THREADS="${BENCHMARK_THREADS:-4}"

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

    if [[ ! -f "$TITAN_BIN" ]]; then
        log_error "Titan binary not found at: $TITAN_BIN"
        exit 1
    fi

    if [[ ! -f "$CONFIG_FILE" ]]; then
        log_error "Config file not found at: $CONFIG_FILE"
        exit 1
    fi

    command -v wrk >/dev/null 2>&1 || { log_error "wrk not found"; exit 1; }

    log_success "All prerequisites satisfied"
}

setup_environment() {
    log_info "Setting up profiling environment..."

    mkdir -p "$PROFILE_DIR"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    SESSION_DIR="$PROFILE_DIR/session_$TIMESTAMP"
    mkdir -p "$SESSION_DIR"

    log_success "Profile results will be saved to: $SESSION_DIR"
}

start_titan_server() {
    log_info "Starting Titan server..."

    # Kill any existing Titan processes
    pkill -9 -f "$TITAN_BIN" 2>/dev/null || true
    sleep 2

    "$TITAN_BIN" --config "$CONFIG_FILE" > "$SESSION_DIR/titan.log" 2>&1 &
    TITAN_PID=$!

    log_info "Waiting for server to be ready (PID: $TITAN_PID)..."
    for i in {1..30}; do
        if curl -s -o /dev/null -w "%{http_code}" "$BENCHMARK_URL" 2>/dev/null | grep -qE "200|404|301|302"; then
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
    if [[ -n "${TITAN_PID:-}" ]]; then
        log_info "Stopping Titan server (PID: $TITAN_PID)..."
        kill -TERM $TITAN_PID 2>/dev/null || true
        sleep 2
        kill -9 $TITAN_PID 2>/dev/null || true
        wait $TITAN_PID 2>/dev/null || true
        log_success "Titan server stopped"
    fi
}

run_benchmark() {
    log_info "Running benchmark (${DURATION}s)..."

    wrk -t$BENCHMARK_THREADS -c$BENCHMARK_CONNECTIONS -d${DURATION}s "$BENCHMARK_URL" \
        > "$SESSION_DIR/benchmark.txt" 2>&1

    log_success "Benchmark complete"
}

analyze_binary() {
    log_info "Analyzing binary..."

    {
        echo "Binary Analysis"
        echo "==============="
        echo ""
        echo "Binary: $TITAN_BIN"
        echo "Size: $(ls -lh "$TITAN_BIN" | awk '{print $5}')"
        echo ""
        echo "Symbol Count: $(nm "$TITAN_BIN" 2>/dev/null | wc -l)"
        echo ""
        echo "Key Symbols:"
        nm "$TITAN_BIN" 2>/dev/null | grep -E "handle_request|process|parse|router" | head -20 || echo "None found"
    } > "$SESSION_DIR/binary_analysis.txt"

    log_success "Binary analysis complete"
}

collect_metrics() {
    log_info "Collecting runtime metrics..."

    if [[ -n "${TITAN_PID:-}" ]] && kill -0 $TITAN_PID 2>/dev/null; then
        {
            echo "Runtime Metrics"
            echo "==============="
            echo ""
            echo "Process Info:"
            ps aux | grep $TITAN_PID | grep -v grep || echo "Process not found"
            echo ""
            echo "Memory Usage:"
            cat /proc/$TITAN_PID/status 2>/dev/null | grep -E "VmSize|VmRSS|VmPeak" || echo "Not available"
            echo ""
            echo "File Descriptors:"
            ls /proc/$TITAN_PID/fd 2>/dev/null | wc -l || echo "Not available"
        } > "$SESSION_DIR/runtime_metrics.txt"

        log_success "Metrics collected"
    else
        log_warning "Server not running, skipping runtime metrics"
    fi
}

generate_summary_report() {
    log_info "Generating summary report..."

    REPORT="$SESSION_DIR/PROFILE_SUMMARY.md"

    cat > "$REPORT" <<EOF
# Titan Profiling Summary (Simple Mode)
**Generated:** $(date)
**Duration:** ${DURATION}s
**Binary:** $TITAN_BIN
**Config:** $CONFIG_FILE

---

## Benchmark Results

\`\`\`
$(cat "$SESSION_DIR/benchmark.txt")
\`\`\`

---

## Binary Analysis

\`\`\`
$(cat "$SESSION_DIR/binary_analysis.txt")
\`\`\`

---

## Runtime Metrics

\`\`\`
$(cat "$SESSION_DIR/runtime_metrics.txt" 2>/dev/null || echo "Not available")
\`\`\`

---

## Performance Summary

EOF

    # Extract key metrics
    if [[ -f "$SESSION_DIR/benchmark.txt" ]]; then
        THROUGHPUT=$(grep "Requests/sec:" "$SESSION_DIR/benchmark.txt" | awk '{print $2}')
        LATENCY_AVG=$(grep "Latency" "$SESSION_DIR/benchmark.txt" | head -1 | awk '{print $2}')
        LATENCY_MAX=$(grep "Latency" "$SESSION_DIR/benchmark.txt" | head -1 | awk '{print $4}')
        REQUESTS=$(grep "requests in" "$SESSION_DIR/benchmark.txt" | awk '{print $1}')
        TRANSFER=$(grep "Transfer/sec:" "$SESSION_DIR/benchmark.txt" | awk '{print $2, $3}')

        echo "- **Throughput:** $THROUGHPUT requests/sec" >> "$REPORT"
        echo "- **Avg Latency:** $LATENCY_AVG" >> "$REPORT"
        echo "- **Max Latency:** $LATENCY_MAX" >> "$REPORT"
        echo "- **Total Requests:** $REQUESTS" >> "$REPORT"
        echo "- **Transfer Rate:** $TRANSFER" >> "$REPORT"
    fi

    echo "" >> "$REPORT"
    echo "---" >> "$REPORT"
    echo "" >> "$REPORT"
    echo "## Files Generated" >> "$REPORT"
    echo "" >> "$REPORT"
    echo "- [benchmark.txt](benchmark.txt) - wrk benchmark output" >> "$REPORT"
    echo "- [binary_analysis.txt](binary_analysis.txt) - Binary symbol analysis" >> "$REPORT"
    echo "- [runtime_metrics.txt](runtime_metrics.txt) - Runtime process metrics" >> "$REPORT"
    echo "- [titan.log](titan.log) - Titan server log" >> "$REPORT"

    log_success "Summary report saved to: $REPORT"

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
    echo "  Titan Profiling Suite (Simple Mode)"
    echo "=================================================="
    echo ""

    trap cleanup EXIT

    check_prerequisites
    setup_environment
    start_titan_server

    # Collect metrics before benchmark
    collect_metrics

    # Run benchmark
    run_benchmark

    # Collect metrics after benchmark
    sleep 2
    collect_metrics

    # Analyze binary
    analyze_binary

    # Generate report
    generate_summary_report

    log_success "Profiling suite completed successfully!"
}

main "$@"
