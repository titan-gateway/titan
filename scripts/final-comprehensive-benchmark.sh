#!/bin/bash

echo "================================================================"
echo "COMPREHENSIVE PROXY BENCHMARK - HTTP/2 vs HTTP/1.1"
echo "Date: $(date)"
echo "================================================================"

# Configuration
REQUESTS=100000
CONNECTIONS_LOW=1000
CONNECTIONS_HIGH=5000
THREADS=4

# Kill all processes
cleanup() {
    pkill -9 titan haproxy envoy nginx python3 2>/dev/null
    sleep 2
}

# Start nginx backend
start_backend() {
    echo "Starting nginx backend on port 3001..."
    nginx -c /workspace/config/nginx-backend.conf
    sleep 2

    # Verify backend is running
    if curl -s http://localhost:3001/ | grep -q "Nginx backend"; then
        echo "✓ Backend started successfully"
    else
        echo "✗ Failed to start backend"
        exit 1
    fi
}

# Benchmark function
benchmark_proxy() {
    local name=$1
    local start_cmd=$2
    local process_name=$3
    local http_endpoint=$4
    local https_endpoint=$5

    echo ""
    echo "=========================================="
    echo "Testing: $name"
    echo "=========================================="

    # Clean and start proxy
    pkill -9 $process_name 2>/dev/null
    sleep 2

    echo "Starting $name..."
    eval "$start_cmd" > /dev/null 2>&1 &
    local proxy_pid=$!
    sleep 4

    # Verify proxy started
    if ! ps -p $proxy_pid > /dev/null; then
        echo "✗ Failed to start $name"
        return
    fi

    echo "$name started (PID: $proxy_pid)"

    # Function to run test and collect metrics
    run_test() {
        local test_name=$1
        local h2load_cmd=$2

        echo ""
        echo "### $test_name ###"

        # Start resource monitoring
        (
            while kill -0 $proxy_pid 2>/dev/null; do
                ps aux | grep "^[^ ]*[ ]*$proxy_pid" | awk '{print $3, $6}' >> /tmp/${name}_${test_name}_resources.txt
                sleep 0.5
            done
        ) &
        local monitor_pid=$!

        # Run h2load
        eval "$h2load_cmd" > /tmp/${name}_${test_name}_results.txt 2>&1

        # Stop monitoring
        kill $monitor_pid 2>/dev/null
        wait $monitor_pid 2>/dev/null

        # Extract metrics
        echo "Results:"
        grep "finished in" /tmp/${name}_${test_name}_results.txt | head -1
        grep "req/s" /tmp/${name}_${test_name}_results.txt | head -1
        grep "time for request:" /tmp/${name}_${test_name}_results.txt | head -1
        grep "status codes:" /tmp/${name}_${test_name}_results.txt | head -1

        # Calculate resource usage
        if [ -f /tmp/${name}_${test_name}_resources.txt ]; then
            awk '{cpu+=$1; mem+=$2; n++} END {
                if(n>0) printf "Resource usage: Avg CPU: %.1f%%, Avg Mem: %.1f MB, Max Mem: %.1f MB\n",
                    cpu/n, mem/(n*1024), mem_max/1024
            }' /tmp/${name}_${test_name}_resources.txt
            rm /tmp/${name}_${test_name}_resources.txt
        fi
    }

    # Run tests based on what endpoints are available
    if [ "$http_endpoint" != "none" ]; then
        run_test "HTTP/1.1-1K" "h2load -n $REQUESTS -c $CONNECTIONS_LOW -t $THREADS --h1 $http_endpoint"
        run_test "HTTP/1.1-5K" "h2load -n $REQUESTS -c $CONNECTIONS_HIGH -t $THREADS --h1 $http_endpoint"
    fi

    if [ "$https_endpoint" != "none" ]; then
        run_test "HTTP/1.1-TLS-1K" "h2load -n $REQUESTS -c $CONNECTIONS_LOW -t $THREADS --h1 $https_endpoint"
        run_test "HTTP/2-1K" "h2load -n $REQUESTS -c $CONNECTIONS_LOW -t $THREADS $https_endpoint"
        run_test "HTTP/2-5K" "h2load -n $REQUESTS -c $CONNECTIONS_HIGH -t $THREADS $https_endpoint"
    fi

    # Kill proxy
    kill $proxy_pid 2>/dev/null
    pkill -9 $process_name 2>/dev/null
}

# Main execution
cleanup
start_backend

# Prepare HAProxy certificate
cat /workspace/certs/cert.pem /workspace/certs/key.pem > /workspace/certs/combined.pem

# Test Titan (only HTTPS on 8443 with /small endpoint)
benchmark_proxy "Titan" \
    "/workspace/build/release/src/titan --config /workspace/benchmarks/configs/titan-http2.json" \
    "titan" \
    "none" \
    "https://localhost:8443/small"

# Test HAProxy (both HTTP and HTTPS with root endpoint)
benchmark_proxy "HAProxy" \
    "haproxy -f /workspace/config/haproxy-bench.cfg" \
    "haproxy" \
    "http://localhost:8080/" \
    "https://localhost:8443/"

# Test Envoy (both HTTP and HTTPS with root endpoint)
benchmark_proxy "Envoy" \
    "envoy -c /workspace/config/envoy-bench.yaml --log-level error" \
    "envoy" \
    "http://localhost:8080/" \
    "https://localhost:8443/"

# Test Nginx (both HTTP and HTTPS with root endpoint)
benchmark_proxy "Nginx" \
    "nginx -c /workspace/config/nginx-proxy-bench.conf" \
    "nginx" \
    "http://localhost:8080/" \
    "https://localhost:8443/"

echo ""
echo "================================================================"
echo "BENCHMARK SUMMARY"
echo "================================================================"

# Generate comparison table
echo ""
echo "Throughput Comparison (req/s):"
echo "-------------------------------"
for proxy in Titan HAProxy Envoy Nginx; do
    echo ""
    echo "$proxy:"
    for test in "HTTP/1.1-1K" "HTTP/1.1-5K" "HTTP/1.1-TLS-1K" "HTTP/2-1K" "HTTP/2-5K"; do
        if [ -f /tmp/${proxy}_${test}_results.txt ]; then
            throughput=$(grep "req/s" /tmp/${proxy}_${test}_results.txt | head -1 | sed -n 's/.*\([0-9.]*\) req\/s.*/\1/p')
            if [ -n "$throughput" ]; then
                printf "  %-20s: %10.2f req/s\n" "$test" "$throughput"
            fi
        fi
    done
done

cleanup
echo ""
echo "Benchmark complete!"