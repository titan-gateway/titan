#!/bin/bash
# Nginx Performance Benchmarking Script
# Benchmarks Nginx reverse proxy to mock-backend

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration - Test NGINX proxy, same as Titan
NGINX_HOST="nginx-baseline"
NGINX_PORT="8081"
DURATION="30s"
CONNECTIONS="100"
THREADS="4"
REQUESTS="10000"

echo -e "${GREEN}=== Nginx Reverse Proxy Benchmarks ===${NC}"
echo "Target: http://${NGINX_HOST}:${NGINX_PORT} (Nginx → mock-backend:3000)"
echo "Duration: ${DURATION}"
echo "Connections: ${CONNECTIONS}"
echo "Threads: ${THREADS}"
echo ""

# Check if Nginx is reachable
echo -e "${YELLOW}Checking if Nginx is reachable...${NC}"
if ! curl -s -o /dev/null -w "%{http_code}" "http://${NGINX_HOST}:${NGINX_PORT}/health" | grep -q "200"; then
    echo "Error: Nginx at http://${NGINX_HOST}:${NGINX_PORT} is not reachable"
    echo "Start it with: docker compose up -d nginx-baseline"
    exit 1
fi
echo -e "${GREEN}Nginx is reachable${NC}"
echo ""

# Create results directory
RESULTS_DIR="/workspace/benchmarks/results"
mkdir -p "${RESULTS_DIR}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_FILE="${RESULTS_DIR}/nginx_${TIMESTAMP}.md"

# Start results file
cat > "${RESULTS_FILE}" << EOF
# Nginx Reverse Proxy Performance Results

**Date:** $(date)
**Target:** http://${NGINX_HOST}:${NGINX_PORT} (Nginx → mock-backend:3000)
**Duration:** ${DURATION}
**Connections:** ${CONNECTIONS}
**Threads:** ${THREADS}

---

EOF

# Test proxied endpoints (not /health which returns static response)
echo -e "${GREEN}=== Running HTTP/1.1 Benchmark (wrk) ===${NC}"
echo "Testing GET / endpoint (proxied to backend)..."
WRK_OUTPUT=$(wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} --latency "http://${NGINX_HOST}:${NGINX_PORT}/" 2>&1)
echo "${WRK_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
## HTTP/1.1 Performance (wrk)

### Endpoint: GET / (proxied)

\`\`\`
${WRK_OUTPUT}
\`\`\`

EOF

echo ""

echo -e "${GREEN}=== Running Apache Bench (ab) ===${NC}"
echo "Testing GET / endpoint..."
AB_OUTPUT=$(ab -n${REQUESTS} -c${CONNECTIONS} "http://${NGINX_HOST}:${NGINX_PORT}/" 2>&1)
echo "${AB_OUTPUT}"

cat >> "${RESULTS_FILE}" << EOF
---

## Apache Bench (ab)

### Endpoint: GET / (proxied)

\`\`\`
${AB_OUTPUT}
\`\`\`

EOF

echo ""
echo -e "${GREEN}=== Benchmark Complete ===${NC}"
echo "Results saved to: ${RESULTS_FILE}"
echo ""

# Extract key metrics
echo -e "${YELLOW}=== Summary ===${NC}"
echo ""
echo "HTTP/1.1 (wrk) - / via Nginx:"
echo "${WRK_OUTPUT}" | grep -E "(Requests/sec|Latency|Transfer/sec)" || true
echo ""

cat >> "${RESULTS_FILE}" << EOF

---

## Summary

**Nginx Reverse Proxy Performance**
- Industry-standard production web server
- Optimized configuration (epoll, keep-alive, sendfile)
- Baseline for comparison with Titan

EOF

echo -e "${GREEN}Nginx benchmark results written to: ${RESULTS_FILE}${NC}"
