#!/bin/bash
# Utility functions for benchmark scripts

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_DIR="$(dirname "$SCRIPT_DIR")"
WORKSPACE_DIR="$(dirname "$BENCHMARK_DIR")"
RESULTS_DIR="$BENCHMARK_DIR/results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored message
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Start mock backend server
start_backend() {
    local port=${1:-3001}

    if pgrep -f "uvicorn main:app.*port $port" > /dev/null; then
        log_info "Backend already running on port $port"
        return 0
    fi

    log_info "Starting backend on port $port..."
    cd "$WORKSPACE_DIR/tests/mock-backend"
    PORT=$port python3 main.py > /tmp/backend-$port.log 2>&1 &
    local pid=$!

    # Wait for backend to start
    for i in {1..10}; do
        if curl -s http://localhost:$port/small > /dev/null 2>&1; then
            log_success "Backend started (PID: $pid)"
            return 0
        fi
        sleep 0.5
    done

    log_error "Backend failed to start"
    return 1
}

# Stop mock backend
stop_backend() {
    local port=${1:-3001}
    pkill -f "uvicorn main:app.*port $port" 2>/dev/null
    log_info "Backend stopped"
}

# Start Nginx
start_nginx() {
    local config="$BENCHMARK_DIR/configs/nginx.conf"

    if pgrep nginx > /dev/null; then
        log_warn "Nginx already running, stopping first..."
        stop_nginx
    fi

    log_info "Starting Nginx..."
    nginx -c "$config" -e /tmp/nginx-error.log

    sleep 1
    if pgrep nginx > /dev/null; then
        log_success "Nginx started"
        return 0
    else
        log_error "Nginx failed to start"
        cat /tmp/nginx-error.log
        return 1
    fi
}

# Check if port is in use
is_port_free() {
    local port=$1
    ! lsof -i :$port > /dev/null 2>&1
}

# Wait for port to be free
wait_for_port_free() {
    local port=$1
    local max_attempts=${2:-20}

    for i in $(seq 1 $max_attempts); do
        if is_port_free $port; then
            return 0
        fi
        sleep 0.5
    done

    log_error "Port $port still in use after $max_attempts attempts"
    # Force kill whatever is using the port
    local pid=$(lsof -t -i :$port 2>/dev/null)
    if [ -n "$pid" ]; then
        log_warn "Force killing process $pid on port $port"
        kill -9 $pid 2>/dev/null
        sleep 1
    fi
    return 1
}

# Stop Nginx
stop_nginx() {
    pkill -9 nginx 2>/dev/null
    wait_for_port_free 8080
    wait_for_port_free 8443
    log_info "Nginx stopped"
}

# Start HAProxy
start_haproxy() {
    local config="$BENCHMARK_DIR/configs/haproxy.cfg"

    if pgrep haproxy > /dev/null; then
        log_warn "HAProxy already running, stopping first..."
        stop_haproxy
    fi

    # Ensure ports are free
    if ! is_port_free 8080 || ! is_port_free 8443; then
        log_warn "Ports still in use, cleaning up..."
        wait_for_port_free 8080
        wait_for_port_free 8443
    fi

    log_info "Starting HAProxy..."
    haproxy -f "$config" -D

    sleep 1
    if pgrep haproxy > /dev/null; then
        log_success "HAProxy started"
        return 0
    else
        log_error "HAProxy failed to start"
        return 1
    fi
}

# Stop HAProxy
stop_haproxy() {
    pkill -9 haproxy 2>/dev/null
    wait_for_port_free 8080
    wait_for_port_free 8443
    log_info "HAProxy stopped"
}

# Start Envoy
start_envoy() {
    local config="$BENCHMARK_DIR/configs/envoy.yaml"

    if pgrep envoy > /dev/null; then
        log_warn "Envoy already running, stopping first..."
        stop_envoy
    fi

    # Ensure ports are free
    if ! is_port_free 8080 || ! is_port_free 8443; then
        log_warn "Ports still in use, cleaning up..."
        wait_for_port_free 8080
        wait_for_port_free 8443
    fi

    log_info "Starting Envoy..."
    envoy -c "$config" --log-level error > /tmp/envoy.log 2>&1 &

    sleep 2
    if pgrep envoy > /dev/null; then
        log_success "Envoy started"
        return 0
    else
        log_error "Envoy failed to start"
        cat /tmp/envoy.log
        return 1
    fi
}

# Stop Envoy
stop_envoy() {
    pkill -9 envoy 2>/dev/null
    wait_for_port_free 8080
    wait_for_port_free 8443
    wait_for_port_free 9901  # Envoy admin port
    log_info "Envoy stopped"
}

# Start Titan
start_titan() {
    local config="$BENCHMARK_DIR/configs/titan.json"

    if pgrep titan > /dev/null; then
        log_warn "Titan already running, stopping first..."
        stop_titan
    fi

    # Ensure ports are free
    if ! is_port_free 8080 || ! is_port_free 8443; then
        log_warn "Ports still in use, cleaning up..."
        wait_for_port_free 8080
        wait_for_port_free 8443
    fi

    log_info "Starting Titan (multi-threaded mode)..."
    "$WORKSPACE_DIR/build/release/src/titan" --config "$config" > /tmp/titan.log 2>&1 &

    sleep 1
    if pgrep titan > /dev/null; then
        log_success "Titan started"
        return 0
    else
        log_error "Titan failed to start"
        cat /tmp/titan.log
        return 1
    fi
}

# Stop Titan
stop_titan() {
    pkill -9 titan 2>/dev/null
    wait_for_port_free 8080
    wait_for_port_free 8443
    wait_for_port_free 9090  # Titan metrics port
    log_info "Titan stopped"
}

# Get CPU and memory usage for a process
get_resource_usage() {
    local process_name=$1
    local pid=$(pgrep -o "$process_name")

    if [ -z "$pid" ]; then
        echo "0,0"
        return
    fi

    # Get CPU% and Memory (RSS in MB)
    local stats=$(ps -p $pid -o %cpu,rss --no-headers 2>/dev/null)
    if [ -z "$stats" ]; then
        echo "0,0"
        return
    fi

    local cpu=$(echo $stats | awk '{print $1}')
    local mem_kb=$(echo $stats | awk '{print $2}')
    local mem_mb=$(echo "scale=2; $mem_kb / 1024" | bc)

    echo "$cpu,$mem_mb"
}

# Wait for URL to be responsive
wait_for_url() {
    local url=$1
    local max_attempts=${2:-20}

    for i in $(seq 1 $max_attempts); do
        if curl -s "$url" > /dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done

    log_error "URL $url not responsive after $max_attempts attempts"
    return 1
}

# Clean up all processes
cleanup_all() {
    log_info "Cleaning up all processes..."
    stop_nginx
    stop_haproxy
    stop_envoy
    stop_titan
    stop_backend
    log_success "Cleanup complete"
}

# Trap to ensure cleanup on exit
trap cleanup_all EXIT INT TERM
