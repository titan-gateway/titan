#!/bin/bash

echo "========================================================================"
echo "PROXY PERFORMANCE BENCHMARK - COMPREHENSIVE COMPARISON"
echo "Date: $(date)"
echo "========================================================================"

# Configuration
REQUESTS=100000
CONNECTIONS_LOW=1000
CONNECTIONS_HIGH=5000

# Ensure clean state
pkill -9 titan 2>/dev/null
pkill -9 haproxy 2>/dev/null
pkill -9 envoy 2>/dev/null
pkill -9 nginx 2>/dev/null
sleep 2

# Start nginx backend if not running
if ! curl -s http://localhost:3001/ | grep -q "Nginx backend"; then
    echo "Starting nginx backend..."
    nginx -c /workspace/config/nginx-backend.conf
    sleep 2
fi

echo "Backend running on port 3001"
echo ""

# Prepare HAProxy cert
cat /workspace/certs/cert.pem /workspace/certs/key.pem > /workspace/certs/combined.pem

# Function to test a proxy
test_proxy() {
    local name=$1
    local start_cmd=$2
    local process_name=$3

    echo "========================================"
    echo "Testing: $name"
    echo "========================================"

    # Kill any existing process
    pkill -9 $process_name 2>/dev/null
    sleep 2

    # Start proxy
    echo "Starting $name..."
    eval "$start_cmd" &
    local proxy_pid=$!
    sleep 3

    # Verify proxy is running
    if ! curl -s -k https://localhost:8443/ >/dev/null 2>&1; then
        echo "Failed to start $name"
        return
    fi

    echo "$name started (PID: $proxy_pid)"
    echo ""

    # Run tests and capture metrics
    for test in "HTTP/1.1-1K" "HTTP/2-1K" "HTTP/1.1-5K" "HTTP/2-5K"; do
        echo "Test: $test"
        echo "----------------------------------------"

        case $test in
            "HTTP/1.1-1K")
                protocol="--h1"
                url="http://localhost:8080/"
                connections=$CONNECTIONS_LOW
                ;;
            "HTTP/2-1K")
                protocol=""
                url="https://localhost:8443/"
                connections=$CONNECTIONS_LOW
                ;;
            "HTTP/1.1-5K")
                protocol="--h1"
                url="http://localhost:8080/"
                connections=$CONNECTIONS_HIGH
                ;;
            "HTTP/2-5K")
                protocol=""
                url="https://localhost:8443/"
                connections=$CONNECTIONS_HIGH
                ;;
        esac

        # Start resource monitoring in background
        (
            while kill -0 $proxy_pid 2>/dev/null; do
                ps aux | grep $proxy_pid | grep -v grep | awk '{print "CPU: "$3"% MEM: "$6/1024"MB"}' >> /tmp/${name}_${test}_resources.txt
                sleep 1
            done
        ) &
        monitor_pid=$!

        # Run h2load
        echo "Running: h2load -n $REQUESTS -c $connections -t 4 $protocol $url"
        h2load -n $REQUESTS -c $connections -t 4 $protocol $url 2>/dev/null > /tmp/${name}_${test}_results.txt

        # Stop monitoring
        kill $monitor_pid 2>/dev/null

        # Extract and display key metrics
        grep "finished in" /tmp/${name}_${test}_results.txt | head -1
        grep "time for request:" /tmp/${name}_${test}_results.txt | head -1
        grep "status codes:" /tmp/${name}_${test}_results.txt | head -1

        # Show resource usage
        if [ -f /tmp/${name}_${test}_resources.txt ]; then
            echo -n "Resource usage: "
            awk '{cpu+=$2; mem+=$4; n++} END {if(n>0) printf "Avg CPU: %.1f%%, Avg Mem: %.1fMB\n", cpu/n, mem/n}' /tmp/${name}_${test}_resources.txt
            rm /tmp/${name}_${test}_resources.txt
        fi
        echo ""
    done

    # Kill proxy
    kill $proxy_pid 2>/dev/null
    pkill -9 $process_name 2>/dev/null
    echo ""
}

# Test each proxy
test_proxy "Titan" "/workspace/build/release/src/titan --config /workspace/benchmarks/configs/titan-http2.json >/dev/null 2>&1" "titan"
test_proxy "HAProxy" "haproxy -f /workspace/config/haproxy-bench.cfg >/dev/null 2>&1" "haproxy"
test_proxy "Envoy" "envoy -c /workspace/config/envoy-bench.yaml --log-level error >/dev/null 2>&1" "envoy"
test_proxy "Nginx" "nginx -c /workspace/config/nginx-proxy-bench.conf" "nginx"

echo "========================================================================"
echo "BENCHMARK SUMMARY"
echo "========================================================================"
echo ""

# Generate summary table
echo "Results saved in /tmp/*_results.txt files"
echo ""

# Extract throughput for comparison
echo "Throughput Comparison (req/s):"
echo "-------------------------------"
for proxy in Titan HAProxy Envoy Nginx; do
    echo ""
    echo "$proxy:"
    for test in "HTTP/1.1-1K" "HTTP/2-1K" "HTTP/1.1-5K" "HTTP/2-5K"; do
        if [ -f /tmp/${proxy}_${test}_results.txt ]; then
            throughput=$(grep "finished in" /tmp/${proxy}_${test}_results.txt | sed -n 's/.*\([0-9.]*\) req\/s.*/\1/p')
            printf "  %-15s: %s req/s\n" "$test" "$throughput"
        fi
    done
done

# Clean up
pkill -9 titan haproxy envoy nginx 2>/dev/null

echo ""
echo "Benchmark complete!"