#!/bin/bash
# Titan Benchmark Cleanup Script
# Ensures clean environment between benchmark runs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "🧹 Cleaning up benchmark environment..."

# Kill all processes
echo "→ Killing processes..."
pkill -9 titan 2>/dev/null || true
pkill -9 nginx 2>/dev/null || true
pkill -9 uvicorn 2>/dev/null || true
pkill -9 python3 2>/dev/null || true
pkill -9 wrk 2>/dev/null || true
pkill -9 h2load 2>/dev/null || true

# Wait for processes to die
sleep 2

# Check and free ports
PORTS=(8080 8443 9090 3001 8081)
for port in "${PORTS[@]}"; do
    pid=$(lsof -ti :$port 2>/dev/null || true)
    if [ -n "$pid" ]; then
        echo "⚠️  Port $port still in use by PID $pid, killing..."
        kill -9 $pid 2>/dev/null || true
        sleep 1
    fi
done

# Verify all ports are free
echo "→ Verifying ports are free..."
for port in "${PORTS[@]}"; do
    if lsof -i :$port >/dev/null 2>&1; then
        echo "❌ ERROR: Port $port is still in use!"
        lsof -i :$port
        exit 1
    fi
done

# Clean up temp files
echo "→ Cleaning temporary files..."
rm -f /tmp/titan-*.log
rm -f /tmp/nginx-*.log
rm -f /tmp/backend-*.log
rm -f /tmp/wrk-*.log
rm -f /tmp/h2load-*.log

# Clean up test artifacts in workspace
if [ -d "/workspace" ]; then
    rm -f /workspace/stress-test.log
    rm -f /workspace/valgrind.log
    rm -f /workspace/*.json.tmp
fi

echo "✅ Cleanup complete - all ports free, all processes killed"
echo ""
