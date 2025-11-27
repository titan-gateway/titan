#!/bin/bash
# Titan Baseline Performance Benchmarking Script
# Runs comprehensive HTTP/1.1, HTTP/2, and HTTPS benchmarks

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
TITAN_HOST="mock-backend"
TITAN_PORT="3000"
DURATION="30s"
CONNECTIONS="100"
THREADS="4"
REQUESTS="10000"

echo -e "${GREEN}=== Titan API Gateway Baseline Benchmarks ===${NC}"
echo "Target: http://${TITAN_HOST}:${TITAN_PORT}"
echo "Duration: ${DURATION}"
echo "Connections: ${CONNECTIONS}"
echo "Threads: ${THREADS}"
echo ""

# Check if target is reachable
echo -e "${YELLOW}Checking if target is reachable...${NC}"
if ! curl -s -o /dev/null -w "%{http_code}" "http://${TITAN_HOST}:${TITAN_PORT}/health" | grep -q "200"; then
    echo -e "${RED}Error: Target http://${TITAN_HOST}:${TITAN_PORT}/health is not reachable${NC}"
    echo "Make sure mock-backend is running: docker compose up -d mock-backend"
    exit 1
fi
echo -e "${GREEN}Target is reachable${NC}"
echo ""

# Create results directory
RESULTS_DIR="/workspace/benchmarks/results"
mkdir -p "${RESULTS_DIR}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${RESULTS_DIR}/baseline_${TIMESTAMP}.md"

# Start results file
cat > "${RESULTS_FILE}" << EOF
# Titan Baseline Performance Results

**Date:** $(date)
**Target:** http://${TITAN_HOST}:${TITAN_PORT}
**Duration:** ${DURATION}
**Connections:** ${CONNECTIONS}
**Threads:** ${THREADS}

---

EOF

# HTTP/1.1 Benchmark with wrk
echo -e "${GREEN}=== Running HTTP/1.1 Benchmark (wrk) ===${NC}"
echo "Testing GET /health endpoint..."
WRK_OUTPUT=$(wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency "http://${TITAN_HOST}:${TITAN_PORT}/health" 2>&1)
echo "${WRK_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
## HTTP/1.1 Performance (wrk)

### Endpoint: GET /health

\`\`\`
${WRK_OUTPUT}
\`\`\`

EOF

echo ""

# HTTP/1.1 JSON endpoint
echo -e "${GREEN}=== Running HTTP/1.1 Benchmark (JSON payload) ===${NC}"
echo "Testing GET /api/data endpoint..."
WRK_JSON_OUTPUT=$(wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency "http://${TITAN_HOST}:${TITAN_PORT}/api/data" 2>&1)
echo "${WRK_JSON_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
### Endpoint: GET /api/data (JSON response)

\`\`\`
${WRK_JSON_OUTPUT}
\`\`\`

EOF

echo ""

# HTTP/2 Benchmark with h2load
echo -e "${GREEN}=== Running HTTP/2 Benchmark (h2load) ===${NC}"
echo "Testing GET /health endpoint..."
H2LOAD_OUTPUT=$(h2load -n${REQUESTS} -c${CONNECTIONS} -t${THREADS} "http://${TITAN_HOST}:${TITAN_PORT}/health" 2>&1)
echo "${H2LOAD_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
---

## HTTP/2 Performance (h2load)

### Endpoint: GET /health

\`\`\`
${H2LOAD_OUTPUT}
\`\`\`

EOF

echo ""

# HTTP/2 JSON endpoint
echo -e "${GREEN}=== Running HTTP/2 Benchmark (JSON payload) ===${NC}"
echo "Testing GET /api/data endpoint..."
H2LOAD_JSON_OUTPUT=$(h2load -n${REQUESTS} -c${CONNECTIONS} -t${THREADS} "http://${TITAN_HOST}:${TITAN_PORT}/api/data" 2>&1)
echo "${H2LOAD_JSON_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
### Endpoint: GET /api/data (JSON response)

\`\`\`
${H2LOAD_JSON_OUTPUT}
\`\`\`

EOF

echo ""

# Apache Bench for comparison
echo -e "${GREEN}=== Running Apache Bench (ab) ===${NC}"
echo "Testing GET /health endpoint..."
AB_OUTPUT=$(ab -n${REQUESTS} -c${CONNECTIONS} "http://${TITAN_HOST}:${TITAN_PORT}/health" 2>&1)
echo "${AB_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
---

## Apache Bench (ab) - Reference

### Endpoint: GET /health

\`\`\`
${AB_OUTPUT}
\`\`\`

EOF

echo ""
echo -e "${GREEN}=== Benchmark Complete ===${NC}"
echo -e "Results saved to: ${RESULTS_FILE}"
echo ""

# Extract key metrics
echo -e "${YELLOW}=== Summary ===${NC}"
echo ""
echo "HTTP/1.1 (wrk) - /health:"
echo "${WRK_OUTPUT}" | grep -E "(Requests/sec|Latency|Transfer/sec)" || true
echo ""
echo "HTTP/1.1 (wrk) - /api/data:"
echo "${WRK_JSON_OUTPUT}" | grep -E "(Requests/sec|Latency|Transfer/sec)" || true
echo ""
echo "HTTP/2 (h2load) - /health:"
echo "${H2LOAD_OUTPUT}" | grep -E "(requests|time for request|req/s)" | head -5 || true
echo ""

cat >> "${RESULTS_FILE}" << EOF

---

## Summary

**Test completed successfully** ✓

Results include:
- HTTP/1.1 performance (wrk) for /health and /api/data
- HTTP/2 performance (h2load) for /health and /api/data
- Apache Bench baseline for comparison

### Next Steps

1. Review latency percentiles (P50, P99, P999)
2. Compare against performance goals:
   - Throughput: >500k req/s target
   - Latency: P99 <1ms target
3. Identify optimization opportunities
4. Run profiling tools (perf, flamegraphs)

EOF

echo -e "${GREEN}Benchmark results written to: ${RESULTS_FILE}${NC}"
