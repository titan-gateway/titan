#!/bin/bash
# Titan Performance & Memory Safety Validation Runner
# Runs comprehensive benchmarks with clean environment between each test

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$WORKSPACE_DIR/benchmarks/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Create results directory
mkdir -p "$RESULTS_DIR"

# Logging
LOG_FILE="$RESULTS_DIR/validation_${TIMESTAMP}.log"
exec > >(tee -a "$LOG_FILE")
exec 2>&1

echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BOLD}   Titan Performance & Memory Safety Validation${NC}"
echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
echo -e "Timestamp: $TIMESTAMP"
echo -e "Log file: $LOG_FILE"
echo ""

# Cleanup function
cleanup() {
    echo -e "${CYAN}→ Running cleanup...${NC}"
    bash "$SCRIPT_DIR/cleanup_benchmark.sh"
}

# Wait for port to be available
wait_for_port() {
    local port=$1
    local max_wait=10
    local waited=0

    while lsof -i :$port >/dev/null 2>&1; do
        if [ $waited -ge $max_wait ]; then
            echo -e "${RED}✗ Timeout waiting for port $port to be free${NC}"
            return 1
        fi
        echo -e "${YELLOW}⏳ Waiting for port $port to be free...${NC}"
        sleep 1
        waited=$((waited + 1))
    done
    echo -e "${GREEN}✓ Port $port is free${NC}"
}

# Wait for service to be ready
wait_for_service() {
    local url=$1
    local max_wait=15
    local waited=0

    while ! curl -sf "$url" >/dev/null 2>&1; do
        if [ $waited -ge $max_wait ]; then
            echo -e "${RED}✗ Timeout waiting for service at $url${NC}"
            return 1
        fi
        echo -e "${YELLOW}⏳ Waiting for service at $url...${NC}"
        sleep 1
        waited=$((waited + 1))
    done
    echo -e "${GREEN}✓ Service at $url is ready${NC}"
}

# Test counter
TESTS_PASSED=0
TESTS_FAILED=0

# Run a benchmark test with cleanup
run_test() {
    local test_name=$1
    local test_func=$2

    echo ""
    echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BOLD}TEST: $test_name${NC}"
    echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

    # Cleanup before test
    cleanup
    sleep 1

    # Run test
    if $test_func; then
        echo -e "${GREEN}✅ PASS: $test_name${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}❌ FAIL: $test_name${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi

    # Cleanup after test
    cleanup
    sleep 2
}

# ═══════════════════════════════════════════════════════════
# Performance Tests
# ═══════════════════════════════════════════════════════════

test_baseline_http1() {
    echo -e "${BLUE}Starting mock backend on port 3001...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"
    .venv/bin/uvicorn main:app --host 127.0.0.1 --port 3001 --log-level warning > /tmp/backend.log 2>&1 &
    BACKEND_PID=$!
    wait_for_service "http://localhost:3001/health" || return 1

    echo -e "${BLUE}Building Titan release binary...${NC}"
    cd "$WORKSPACE_DIR"
    cmake --build --preset=release -j4 || return 1

    echo -e "${BLUE}Starting Titan on port 8080...${NC}"
    ./build/release/src/titan --config config/benchmark.json > /tmp/titan.log 2>&1 &
    TITAN_PID=$!
    sleep 3
    wait_for_service "http://localhost:8080/health" || return 1

    echo -e "${BLUE}Running wrk benchmark (30 seconds)...${NC}"
    wrk -t4 -c100 -d30s http://localhost:8080/api > "$RESULTS_DIR/baseline_http1_${TIMESTAMP}.txt"

    # Parse results
    local rps=$(grep "Requests/sec" "$RESULTS_DIR/baseline_http1_${TIMESTAMP}.txt" | awk '{print $2}')
    echo -e "${CYAN}Throughput: $rps req/s${NC}"

    # Check for errors
    if grep -q "Socket errors" "$RESULTS_DIR/baseline_http1_${TIMESTAMP}.txt"; then
        echo -e "${RED}Socket errors detected!${NC}"
        return 1
    fi

    return 0
}

test_baseline_http2() {
    echo -e "${BLUE}Starting mock backend on port 3001...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"
    .venv/bin/uvicorn main:app --host 127.0.0.1 --port 3001 --log-level warning > /tmp/backend.log 2>&1 &
    BACKEND_PID=$!
    wait_for_service "http://localhost:3001/health" || return 1

    echo -e "${BLUE}Starting Titan on port 8443 (HTTPS)...${NC}"
    cd "$WORKSPACE_DIR"
    ./build/release/src/titan --config config/benchmark-https.json > /tmp/titan.log 2>&1 &
    TITAN_PID=$!
    sleep 3

    echo -e "${BLUE}Running h2load benchmark (100k requests)...${NC}"
    h2load -t4 -c100 -n100000 https://localhost:8443/api > "$RESULTS_DIR/baseline_http2_${TIMESTAMP}.txt" || true

    # Parse results
    local rps=$(grep "finished in" "$RESULTS_DIR/baseline_http2_${TIMESTAMP}.txt" | awk '{print $(NF-1)}')
    echo -e "${CYAN}Throughput: $rps req/s${NC}"

    return 0
}

test_compression_overhead() {
    echo -e "${BLUE}Starting mock backend on port 3001...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"
    .venv/bin/uvicorn main:app --host 127.0.0.1 --port 3001 --log-level warning > /tmp/backend.log 2>&1 &
    BACKEND_PID=$!
    wait_for_service "http://localhost:3001/health" || return 1

    echo -e "${BLUE}Starting Titan with compression enabled...${NC}"
    cd "$WORKSPACE_DIR"
    ./build/release/src/titan --config config/benchmark-compression.json > /tmp/titan.log 2>&1 &
    TITAN_PID=$!
    sleep 3
    wait_for_service "http://localhost:8080/health" || return 1

    echo -e "${BLUE}Running wrk with Accept-Encoding: zstd...${NC}"
    wrk -t4 -c100 -d30s -H "Accept-Encoding: zstd" http://localhost:8080/api > "$RESULTS_DIR/compression_zstd_${TIMESTAMP}.txt"

    # Parse results
    local rps=$(grep "Requests/sec" "$RESULTS_DIR/compression_zstd_${TIMESTAMP}.txt" | awk '{print $2}')
    echo -e "${CYAN}Throughput with compression: $rps req/s${NC}"

    # Check compression metrics
    echo -e "${BLUE}Checking compression metrics...${NC}"
    curl -s http://localhost:9090/metrics | grep -E "compression_requests|compression_ratio" > "$RESULTS_DIR/compression_metrics_${TIMESTAMP}.txt"

    return 0
}

test_stress_10min() {
    echo -e "${YELLOW}⚠️  Long-duration test (10 minutes)${NC}"

    echo -e "${BLUE}Starting mock backend on port 3001...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"
    .venv/bin/uvicorn main:app --host 127.0.0.1 --port 3001 --log-level warning > /tmp/backend.log 2>&1 &
    BACKEND_PID=$!
    wait_for_service "http://localhost:3001/health" || return 1

    echo -e "${BLUE}Starting Titan...${NC}"
    cd "$WORKSPACE_DIR"
    ./build/release/src/titan --config config/benchmark.json > /tmp/titan.log 2>&1 &
    TITAN_PID=$!
    sleep 3
    wait_for_service "http://localhost:8080/health" || return 1

    echo -e "${BLUE}Running 10-minute stress test...${NC}"
    wrk -t4 -c200 -d600s http://localhost:8080/api > "$RESULTS_DIR/stress_10min_${TIMESTAMP}.txt"

    # Check for connection leaks
    local close_wait=$(ss -tan | grep CLOSE_WAIT | wc -l)
    echo -e "${CYAN}CLOSE_WAIT connections: $close_wait${NC}"

    if [ "$close_wait" -gt 0 ]; then
        echo -e "${RED}Connection leak detected!${NC}"
        return 1
    fi

    return 0
}

# ═══════════════════════════════════════════════════════════
# Memory Safety Tests
# ═══════════════════════════════════════════════════════════

test_asan_unit_tests() {
    echo -e "${BLUE}Building with AddressSanitizer...${NC}"
    cd "$WORKSPACE_DIR"

    # Clean build with ASAN
    rm -rf build/dev
    cmake --preset=dev \
        -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" || return 1
    cmake --build --preset=dev -j4 || return 1

    echo -e "${BLUE}Running unit tests with ASAN...${NC}"
    ctest --preset=test --output-on-failure > "$RESULTS_DIR/asan_unit_${TIMESTAMP}.log" 2>&1

    # Check for ASAN errors
    if grep -q "ERROR: AddressSanitizer" "$RESULTS_DIR/asan_unit_${TIMESTAMP}.log"; then
        echo -e "${RED}AddressSanitizer detected errors!${NC}"
        grep "ERROR: AddressSanitizer" "$RESULTS_DIR/asan_unit_${TIMESTAMP}.log"
        return 1
    fi

    echo -e "${GREEN}No ASAN errors in unit tests${NC}"
    return 0
}

test_asan_integration_tests() {
    echo -e "${BLUE}Running integration tests with ASAN...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"

    .venv/bin/pytest -v --tb=short > "$RESULTS_DIR/asan_integration_${TIMESTAMP}.log" 2>&1
    local exit_code=$?

    if [ $exit_code -ne 0 ]; then
        echo -e "${RED}Integration tests failed${NC}"
        return 1
    fi

    echo -e "${GREEN}All integration tests passed with ASAN${NC}"
    return 0
}

test_valgrind_memcheck() {
    echo -e "${BLUE}Building debug binary for valgrind...${NC}"
    cd "$WORKSPACE_DIR"

    # Clean debug build (no ASAN for valgrind)
    rm -rf build/dev
    cmake --preset=dev || return 1
    cmake --build --preset=dev -j4 || return 1

    echo -e "${BLUE}Starting mock backend...${NC}"
    cd "$WORKSPACE_DIR/tests/integration"
    .venv/bin/uvicorn main:app --host 127.0.0.1 --port 3001 --log-level warning > /tmp/backend.log 2>&1 &
    BACKEND_PID=$!
    wait_for_service "http://localhost:3001/health" || return 1

    echo -e "${BLUE}Starting Titan under valgrind...${NC}"
    cd "$WORKSPACE_DIR"
    valgrind --leak-check=full --show-leak-kinds=all \
        --track-origins=yes --log-file="$RESULTS_DIR/valgrind_${TIMESTAMP}.log" \
        ./build/dev/src/titan --config config/benchmark.json > /tmp/titan.log 2>&1 &
    TITAN_PID=$!

    # Wait for Titan to start
    sleep 10

    echo -e "${BLUE}Running short load test...${NC}"
    wrk -t2 -c50 -d30s http://localhost:8080/api > /dev/null 2>&1 || true

    echo -e "${BLUE}Stopping Titan gracefully...${NC}"
    kill -INT $TITAN_PID
    sleep 5

    # Check valgrind output
    if grep -q "definitely lost:" "$RESULTS_DIR/valgrind_${TIMESTAMP}.log"; then
        local lost=$(grep "definitely lost:" "$RESULTS_DIR/valgrind_${TIMESTAMP}.log" | awk '{print $4}')
        if [ "$lost" != "0" ]; then
            echo -e "${RED}Memory leaks detected: $lost bytes${NC}"
            return 1
        fi
    fi

    echo -e "${GREEN}No memory leaks detected${NC}"
    return 0
}

# ═══════════════════════════════════════════════════════════
# Main Execution
# ═══════════════════════════════════════════════════════════

main() {
    # Initial cleanup
    cleanup

    echo -e "${BOLD}Running Performance Tests...${NC}"
    run_test "HTTP/1.1 Baseline" test_baseline_http1
    run_test "HTTP/2 Baseline" test_baseline_http2
    run_test "Compression Overhead (Zstd)" test_compression_overhead

    # Ask before long test
    echo ""
    echo -e "${YELLOW}Next: 10-minute stress test${NC}"
    read -p "Continue with 10-minute stress test? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        run_test "10-Minute Stress Test" test_stress_10min
    else
        echo -e "${YELLOW}Skipping stress test${NC}"
    fi

    echo ""
    echo -e "${BOLD}Running Memory Safety Tests...${NC}"
    run_test "ASAN Unit Tests" test_asan_unit_tests
    run_test "ASAN Integration Tests" test_asan_integration_tests
    run_test "Valgrind Memcheck" test_valgrind_memcheck

    # Final cleanup
    cleanup

    # Summary
    echo ""
    echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}   Validation Summary${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}Tests Passed: $TESTS_PASSED${NC}"
    echo -e "${RED}Tests Failed: $TESTS_FAILED${NC}"
    echo -e "Results directory: $RESULTS_DIR"
    echo -e "Log file: $LOG_FILE"
    echo ""

    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}${BOLD}✅ ALL TESTS PASSED${NC}"
        return 0
    else
        echo -e "${RED}${BOLD}❌ SOME TESTS FAILED${NC}"
        return 1
    fi
}

# Run main
main "$@"
