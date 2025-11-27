#!/bin/bash
# Titan Performance Benchmarking Script
# Benchmarks Titan API Gateway (not the backend directly)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration - Test TITAN, not backend directly!
TITAN_HOST="localhost"
TITAN_PORT="8080"
DURATION="30s"
CONNECTIONS="100"
THREADS="4"
REQUESTS="10000"

echo -e "${GREEN}=== Titan API Gateway Performance Benchmarks ===${NC}"
echo "Target: http://${TITAN_HOST}:${TITAN_PORT} (Titan proxying to mock-backend)"
echo "Duration: ${DURATION}"
echo "Connections: ${CONNECTIONS}"
echo "Threads: ${THREADS}"
echo ""

# Check if Titan is reachable
echo -e "${YELLOW}Checking if Titan is reachable...${NC}"
if ! curl -s -o /dev/null -w "%{http_code}" "http://${TITAN_HOST}:${TITAN_PORT}/health" | grep -q "200"; then
    echo -e "${RED}Error: Titan at http://${TITAN_HOST}:${TITAN_PORT}/health is not reachable${NC}"
    echo "Make sure Titan is running: ./build/dev/src/titan --config config/benchmark.json"
    exit 1
fi
echo -e "${GREEN}Titan is reachable${NC}"
echo ""

# Create results directory
RESULTS_DIR="/workspace/benchmarks/results"
mkdir -p "${RESULTS_DIR}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${RESULTS_DIR}/titan_${TIMESTAMP}.md"

# Start results file
cat > "${RESULTS_FILE}" << EOF
# Titan API Gateway Performance Results

**Date:** $(date)
**Target:** http://${TITAN_HOST}:${TITAN_PORT} (Titan → mock-backend)
**Duration:** ${DURATION}
**Connections:** ${CONNECTIONS}
**Threads:** ${THREADS}

---

EOF

# HTTP/1.1 Benchmark with wrk
echo -e "${GREEN}=== Running HTTP/1.1 Benchmark (wrk) ===${NC}"
echo "Testing GET /health endpoint through Titan..."
WRK_OUTPUT=$(wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency "http://${TITAN_HOST}:${TITAN_PORT}/health" 2>&1)
echo "${WRK_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
## HTTP/1.1 Performance (wrk)

### Endpoint: GET /health (via Titan)

\`\`\`
${WRK_OUTPUT}
\`\`\`

EOF

echo ""

# HTTP/1.1 JSON endpoint
echo -e "${GREEN}=== Running HTTP/1.1 Benchmark (JSON payload) ===${NC}"
echo "Testing GET /api/data endpoint through Titan..."
WRK_JSON_OUTPUT=$(wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency "http://${TITAN_HOST}:${TITAN_PORT}/api/data" 2>&1)
echo "${WRK_JSON_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
### Endpoint: GET /api/data (JSON response via Titan)

\`\`\`
${WRK_JSON_OUTPUT}
\`\`\`

EOF

echo ""

# Apache Bench for comparison
echo -e "${GREEN}=== Running Apache Bench (ab) ===${NC}"
echo "Testing GET /health endpoint through Titan..."
AB_OUTPUT=$(ab -n${REQUESTS} -c${CONNECTIONS} "http://${TITAN_HOST}:${TITAN_PORT}/health" 2>&1)
echo "${AB_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
---

## Apache Bench (ab) - Reference

### Endpoint: GET /health (via Titan)

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
echo "HTTP/1.1 (wrk) - /health via Titan:"
echo "${WRK_OUTPUT}" | grep -E "(Requests/sec|Latency|Transfer/sec)" || true
echo ""
echo "HTTP/1.1 (wrk) - /api/data via Titan:"
echo "${WRK_JSON_OUTPUT}" | grep -E "(Requests/sec|Latency|Transfer/sec)" || true
echo ""

cat >> "${RESULTS_FILE}" << EOF

---

## Summary

**Test completed successfully** ✓

### Performance Metrics

**Titan API Gateway Performance:**
- Tested through Titan proxy (localhost:8080 → mock-backend:3000)
- HTTP/1.1 latency and throughput measured
- Poll-based event loop architecture

### Next Steps

1. Compare Titan performance vs direct backend performance
2. Calculate proxy overhead (Titan latency - backend latency)
3. Identify optimization opportunities
4. Profile hot paths with perf
5. Optimize based on profiling data

EOF

echo -e "${GREEN}Titan benchmark results written to: ${RESULTS_FILE}${NC}"
