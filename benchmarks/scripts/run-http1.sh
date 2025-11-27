#!/bin/bash
# HTTP/1.1 Benchmark Runner
# Tests all proxies with wrk across multiple scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/utils.sh"

# Configuration
DURATION=30
THREADS=4
CONNECTIONS=100
WARMUP=5

# Scenario to run (default: all)
SCENARIO="${1:-all}"

# Test scenarios
declare -A SCENARIOS
SCENARIOS[small]="Small Response (1KB)"
SCENARIOS[medium]="Medium Response (10KB)"
SCENARIOS[large]="Large Response (100KB)"
SCENARIOS[high-concurrency]="High Concurrency (2000 connections)"
SCENARIOS[keepalive]="Keep-Alive Stress (100k requests)"

# Proxies to test
PROXIES=("nginx" "haproxy" "envoy" "titan")

echo "========================================"
echo "Titan Benchmark Suite - HTTP/1.1"
echo "========================================"
echo "Duration: ${DURATION}s (+ ${WARMUP}s warmup)"
echo "Threads: $THREADS"
echo "Connections: $CONNECTIONS"
echo "Scenario: $SCENARIO"
echo ""

# Check if backend is running
log_info "Ensuring backend is running..."
start_backend 3001

# Function to run wrk benchmark
run_wrk_bench() {
    local proxy=$1
    local scenario=$2
    local url=$3
    local extra_args=$4

    log_info "Running wrk benchmark..."

    # Warmup
    wrk -t$THREADS -c$CONNECTIONS -d${WARMUP}s "$url" > /dev/null 2>&1

    # Actual benchmark
    local output=$(wrk -t$THREADS -c$CONNECTIONS -d${DURATION}s $extra_args "$url" 2>&1)

    # Parse wrk output
    local req_per_sec=$(echo "$output" | grep "Requests/sec:" | awk '{print $2}')
    local transfer_per_sec=$(echo "$output" | grep "Transfer/sec:" | awk '{print $2}')
    local latency_avg=$(echo "$output" | grep "Latency" | awk '{print $2}')
    local latency_p50=$(echo "$output" | grep "50%" | awk '{print $2}')
    local latency_p99=$(echo "$output" | grep "99%" | awk '{print $2}')

    # Get resource usage
    local usage=$(get_resource_usage "$proxy")
    local cpu=$(echo $usage | cut -d',' -f1)
    local mem=$(echo $usage | cut -d',' -f2)

    # Count errors
    local errors=$(echo "$output" | grep -oP "Non-2xx.*: \K\d+" || echo "0")
    local total_requests=$(echo "$output" | grep "requests in" | awk '{print $1}')

    echo ""
    log_success "Benchmark complete"
    echo "  Requests/sec:   $(printf "%'.2f" $req_per_sec)"
    echo "  Transfer/sec:   $transfer_per_sec"
    echo "  Latency (avg):  $latency_avg"
    echo "  Latency (p99):  $latency_p99"
    echo "  CPU:            ${cpu}%"
    echo "  Memory:         ${mem} MB"
    echo "  Total Requests: $total_requests"
    echo "  Errors:         $errors"
    echo ""

    # Save results as JSON
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    local result_file="$RESULTS_DIR/http1-${scenario}-${proxy}-$(date +%s).json"

    cat > "$result_file" << EOF
{
  "proxy": "$proxy",
  "protocol": "http1",
  "scenario": "$scenario",
  "timestamp": "$timestamp",
  "config": {
    "duration": $DURATION,
    "threads": $THREADS,
    "connections": $CONNECTIONS
  },
  "metrics": {
    "requests_per_sec": $req_per_sec,
    "transfer_per_sec": "$transfer_per_sec",
    "latency_avg": "$latency_avg",
    "latency_p50": "$latency_p50",
    "latency_p99": "$latency_p99",
    "cpu_percent": $cpu,
    "memory_mb": $mem,
    "total_requests": "$total_requests",
    "errors": $errors
  }
}
EOF

    log_success "Results saved to $result_file"
}

# Function to test a single scenario
test_scenario() {
    local scenario=$1
    local scenario_name="${SCENARIOS[$scenario]}"

    echo ""
    echo "========================================"
    echo "Scenario: $scenario_name"
    echo "========================================"

    # Determine URL and parameters based on scenario
    local url="http://localhost:8080/$scenario"
    local extra_args=""

    case $scenario in
        high-concurrency)
            CONNECTIONS=2000
            url="http://localhost:8080/small"
            ;;
        keepalive)
            extra_args="-c100"
            url="http://localhost:8080/small"
            CONNECTIONS=100
            ;;
    esac

    # Test each proxy
    for proxy in "${PROXIES[@]}"; do
        echo ""
        echo "----------------------------------------"
        echo "Testing: $proxy"
        echo "----------------------------------------"

        # Start proxy
        case $proxy in
            nginx)
                start_nginx || continue
                ;;
            haproxy)
                start_haproxy || continue
                ;;
            envoy)
                start_envoy || continue
                ;;
            titan)
                # Build Titan if needed
                if [ ! -f "$WORKSPACE_DIR/build/release/src/titan" ]; then
                    log_info "Building Titan (release)..."
                    cd "$WORKSPACE_DIR"
                    cmake --preset=release && cmake --build --preset=release
                fi
                start_titan || continue
                ;;
        esac

        # Wait for proxy to be ready
        wait_for_url "http://localhost:8080/$scenario"

        # Run benchmark
        run_wrk_bench "$proxy" "$scenario" "$url" "$extra_args"

        # Stop proxy
        case $proxy in
            nginx) stop_nginx ;;
            haproxy) stop_haproxy ;;
            envoy) stop_envoy ;;
            titan) stop_titan ;;
        esac

        sleep 2
    done

    # Reset connection count
    CONNECTIONS=100
}

# Main execution
if [ "$SCENARIO" = "all" ]; then
    for scenario in small medium large high-concurrency keepalive; do
        test_scenario "$scenario"
    done
else
    if [ -z "${SCENARIOS[$SCENARIO]}" ]; then
        log_error "Unknown scenario: $SCENARIO"
        echo "Available scenarios:"
        for key in "${!SCENARIOS[@]}"; do
            echo "  - $key: ${SCENARIOS[$key]}"
        done
        exit 1
    fi

    test_scenario "$SCENARIO"
fi

echo ""
echo "========================================"
echo "All HTTP/1.1 benchmarks complete!"
echo "========================================"
echo ""
echo "Results saved to: $RESULTS_DIR"
echo ""
echo "To compare results:"
echo "  ./scripts/compare.py $RESULTS_DIR/http1-*.json"
echo ""
