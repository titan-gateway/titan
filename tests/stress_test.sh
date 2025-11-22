#!/usr/bin/env bash
# Titan Performance Benchmarking Script
# Uses wrk for HTTP load testing with configurable parameters

set -euo pipefail

# Configuration
TITAN_URL="${TITAN_URL:-http://127.0.0.1:8080}"
DURATION="${DURATION:-30s}"
CONNECTIONS="${CONNECTIONS:-100}"
THREADS="${THREADS:-4}"
PATH_ENDPOINT="${PATH_ENDPOINT:-/}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Titan HTTP Load Testing Script

OPTIONS:
    -u URL          Target URL (default: http://127.0.0.1:8080)
    -d DURATION     Test duration (default: 30s)
    -c CONNECTIONS  Number of connections (default: 100)
    -t THREADS      Number of threads (default: 4)
    -p PATH         Path to test (default: /)
    -h              Show this help

EXAMPLES:
    # Quick test (5 seconds, 10 connections)
    $0 -d 5s -c 10

    # Stress test (1 minute, 1000 connections)
    $0 -d 1m -c 1000 -t 8

    # Test specific endpoint
    $0 -p /api/users/1 -d 10s
EOF
    exit 0
}

# Parse arguments
while getopts "u:d:c:t:p:h" opt; do
    case $opt in
        u) TITAN_URL="$OPTARG" ;;
        d) DURATION="$OPTARG" ;;
        c) CONNECTIONS="$OPTARG" ;;
        t) THREADS="$OPTARG" ;;
        p) PATH_ENDPOINT="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

# Validate wrk is installed
if ! command -v wrk &> /dev/null; then
    echo -e "${RED}Error: wrk not found${NC}"
    echo "Install it with: apt-get install wrk"
    exit 1
fi

# Build full URL
FULL_URL="${TITAN_URL}${PATH_ENDPOINT}"

echo -e "${GREEN}=== Titan Performance Benchmark ===${NC}"
echo "Target:      $FULL_URL"
echo "Duration:    $DURATION"
echo "Connections: $CONNECTIONS"
echo "Threads:     $THREADS"
echo ""

# Check if server is responding
echo -n "Checking server availability... "
if curl -sf -m 2 "$FULL_URL" > /dev/null 2>&1; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
    echo -e "${RED}Error: Server not responding at $FULL_URL${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}Starting benchmark...${NC}"
echo ""

# Run wrk benchmark
wrk -t${THREADS} \
    -c${CONNECTIONS} \
    -d${DURATION} \
    --latency \
    --timeout 10s \
    "${FULL_URL}"

echo ""
echo -e "${GREEN}=== Benchmark Complete ===${NC}"
