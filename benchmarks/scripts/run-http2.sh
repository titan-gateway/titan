#!/bin/bash
# HTTP/2 Benchmark Runner
# Tests all proxies with h2load

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/utils.sh"

# Configuration
DURATION=30
CLIENTS=10
STREAMS=100
WARMUP=5

# Scenario to run (default: all)
SCENARIO="${1:-all}"

# Test scenarios
declare -A SCENARIOS
SCENARIOS[small]="Small Response (1KB, multiplexing)"
SCENARIOS[medium]="Medium Response (10KB, multiplexing)"
SCENARIOS[high-streams]="High Stream Concurrency (1000 streams)"

# Proxies to test (HTTP/2 requires TLS)
PROXIES=("nginx" "haproxy" "envoy" "titan")

echo "========================================"
echo "Titan Benchmark Suite - HTTP/2"
echo "========================================"
echo "Duration: ${DURATION}s (+ ${WARMUP}s warmup)"
echo "Clients: $CLIENTS"
echo "Streams: $STREAMS"
echo "Scenario: $SCENARIO"
echo ""

# Check if backend is running
log_info "Ensuring backend is running..."
start_backend 3001

# Function to run h2load benchmark
run_h2load_bench() {
    local proxy=$1
    local scenario=$2
    local url=$3
    local extra_args=$4

    log_info "Running h2load benchmark..."

    # Warmup
    h2load -c$CLIENTS -m$STREAMS -t1 -D${WARMUP} "$url" > /dev/null 2>&1

    # Actual benchmark
    local output=$(h2load -c$CLIENTS -m$STREAMS -t1 -D${DURATION} $extra_args "$url" 2>&1)

    # Parse h2load output (format: "finished in 5.01s, 20000.00 req/s, 20.29MB/s")
    local req_per_sec=$(echo "$output" | grep "req/s" | awk '{print $4}' | tr -d ',')

    # Parse latency line (format: "time for request:     6.74ms     77.31ms     42.26ms      2.96ms    97.81%")
    local latency_line=$(echo "$output" | grep "time for request")
    local latency_min=$(echo "$latency_line" | awk '{print $4}')
    local latency_max=$(echo "$latency_line" | awk '{print $5}')
    local latency_mean=$(echo "$latency_line" | awk '{print $6}')

    # Get resource usage
    local usage=$(get_resource_usage "$proxy")
    local cpu=$(echo $usage | cut -d',' -f1)
    local mem=$(echo $usage | cut -d',' -f2)

    # Count successful/failed requests (format: "requests: 100011 total, 100011 started, 100011 done, 100000 succeeded, 11 failed, 11 errored, 0 timeout")
    local requests_line=$(echo "$output" | grep "^requests:")
    local total_requests=$(echo "$requests_line" | awk '{print $2}' | tr -d ',')
    local succeeded=$(echo "$requests_line" | awk '{print $8}' | tr -d ',')
    local failed=$(echo "$requests_line" | awk '{print $10}' | tr -d ',')

    echo ""
    log_success "Benchmark complete"
    echo "  Requests/sec:   $(printf "%'.2f" $req_per_sec)"
    echo "  Latency (mean): $latency_mean"
    echo "  Latency (min):  $latency_min"
    echo "  Latency (max):  $latency_max"
    echo "  CPU:            ${cpu}%"
    echo "  Memory:         ${mem} MB"
    echo "  Total Requests: $total_requests"
    echo "  Succeeded:      $succeeded"
    echo "  Failed:         $failed"
    echo ""

    # Save results as JSON
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    local result_file="$RESULTS_DIR/http2-${scenario}-${proxy}-$(date +%s).json"

    cat > "$result_file" << EOF
{
  "proxy": "$proxy",
  "protocol": "http2",
  "scenario": "$scenario",
  "timestamp": "$timestamp",
  "config": {
    "duration": $DURATION,
    "clients": $CLIENTS,
    "streams": $STREAMS
  },
  "metrics": {
    "requests_per_sec": $req_per_sec,
    "latency_mean": "$latency_mean",
    "latency_min": "$latency_min",
    "latency_max": "$latency_max",
    "cpu_percent": $cpu,
    "memory_mb": $mem,
    "total_requests": "$total_requests",
    "succeeded": "$succeeded",
    "failed": "$failed"
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
    local url="https://localhost:8443/$scenario"
    local extra_args=""

    case $scenario in
        high-streams)
            STREAMS=1000
            CLIENTS=50
            url="https://localhost:8443/small"
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

                # Start Titan with HTTP/2 TLS config
                if pgrep titan > /dev/null; then
                    log_warn "Titan already running, stopping first..."
                    pkill -9 titan 2>/dev/null
                    wait_for_port_free 8443
                fi

                log_info "Starting Titan (HTTP/2 with TLS)..."
                "$WORKSPACE_DIR/build/release/src/titan" --config "$BENCHMARK_DIR/configs/titan-http2.json" > /tmp/titan.log 2>&1 &

                sleep 2
                if ! pgrep titan > /dev/null; then
                    log_error "Titan failed to start"
                    cat /tmp/titan.log
                    continue
                fi
                log_success "Titan started"
                ;;
        esac

        # Wait for proxy to be ready
        sleep 2

        # Run benchmark (h2load accepts self-signed certs by default)
        run_h2load_bench "$proxy" "$scenario" "$url" ""

        # Stop proxy
        case $proxy in
            nginx) stop_nginx ;;
            haproxy) stop_haproxy ;;
            envoy) stop_envoy ;;
            titan) stop_titan ;;
        esac

        sleep 2
    done

    # Reset parameters
    STREAMS=100
    CLIENTS=10
}

# Main execution
if [ "$SCENARIO" = "all" ]; then
    for scenario in small medium high-streams; do
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
echo "All HTTP/2 benchmarks complete!"
echo "========================================"
echo ""
echo "Results saved to: $RESULTS_DIR"
echo ""
echo "To compare results:"
echo "  ./scripts/compare.py $RESULTS_DIR/http2-*.json"
echo ""
