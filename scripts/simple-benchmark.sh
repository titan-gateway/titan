#!/bin/bash

# Simple benchmark script
echo "=========================================="
echo "PROXY PERFORMANCE BENCHMARK"
echo "Date: $(date)"
echo "=========================================="

# Test parameters
REQUESTS=100000
CONNECTIONS=1000
THREADS=4

# Clean function
cleanup() {
    pkill -9 titan haproxy envoy nginx 2>/dev/null
    sleep 1
}

# Benchmark function
benchmark_proxy() {
    local name=$1
    local start_cmd=$2
    local test_name=$3

    echo ""
    echo "Testing $name ($test_name)"
    echo "----------------------------------------"

    # Start proxy
    cleanup
    sleep 1
    eval "$start_cmd &"
    sleep 3

    # Verify proxy is running
    if ! curl -s -k https://localhost:8443/ > /dev/null 2>&1; then
        echo "  ✗ Failed to start $name"
        return
    fi

    # Run h2load test
    echo "  Running h2load..."
    h2load -n $REQUESTS -c $CONNECTIONS -t $THREADS --h1 https://localhost:8443/ 2>/dev/null | grep -E 'finished in|req/s|time for request' | head -3

    # Get resource usage
    local pid=$(pgrep -f "$name" | head -1)
    if [ -n "$pid" ]; then
        ps aux | grep -E "PID|$pid" | head -2
    fi
}

# Ensure nginx backend is running
if ! curl -s http://localhost:3001/ | grep -q "Nginx backend"; then
    echo "Starting nginx backend..."
    nginx -c /workspace/config/nginx-backend.conf
    sleep 2
fi

# Test Titan
benchmark_proxy "Titan" "/workspace/build/release/src/titan --config /workspace/benchmarks/configs/titan-http2.json" "HTTP/1.1+TLS"

# Test HAProxy
cat /workspace/certs/cert.pem /workspace/certs/key.pem > /workspace/certs/combined.pem
benchmark_proxy "HAProxy" "haproxy -f /workspace/config/haproxy-bench.cfg" "HTTP/1.1+TLS"

# Test Envoy
benchmark_proxy "Envoy" "envoy -c /workspace/config/envoy-bench.yaml" "HTTP/1.1+TLS"

# Test Nginx
benchmark_proxy "Nginx" "nginx -c /workspace/config/nginx-proxy-bench.conf" "HTTP/1.1+TLS"

# Clean up
cleanup

echo ""
echo "=========================================="
echo "Benchmark Complete"
echo "=========================================="