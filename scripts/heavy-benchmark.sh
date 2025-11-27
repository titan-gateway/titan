#!/bin/bash
set -e

TARGET=${1:-http://localhost:8080}
OUTPUT_PREFIX=${2:-heavy-bench}

echo "========================================"
echo "HEAVY LOAD BENCHMARK - 2 MINUTE TESTS"
echo "Target: $TARGET"
echo "========================================"
echo

# Test 1: High Concurrency - Root Endpoint
echo "[1/4] High Concurrency - 2000 connections, 2 minutes"
wrk -t8 -c2000 -d120s --latency $TARGET/ > ${OUTPUT_PREFIX}-2k-root.txt
cat ${OUTPUT_PREFIX}-2k-root.txt
echo

# Test 2: Extreme Concurrency - Root Endpoint  
echo "[2/4] Extreme Concurrency - 5000 connections, 2 minutes"
wrk -t8 -c5000 -d120s --latency $TARGET/ > ${OUTPUT_PREFIX}-5k-root.txt
cat ${OUTPUT_PREFIX}-5k-root.txt
echo

# Test 3: High Concurrency - Backend Proxy (JSON)
echo "[3/4] High Concurrency - 2000 connections, 2 minutes (Backend Proxy)"
wrk -t8 -c2000 -d120s --latency $TARGET/api/users/123 > ${OUTPUT_PREFIX}-2k-json.txt
cat ${OUTPUT_PREFIX}-2k-json.txt
echo

# Test 4: Extreme Concurrency - Backend Proxy (JSON)
echo "[4/4] Extreme Concurrency - 5000 connections, 2 minutes (Backend Proxy)"
wrk -t8 -c5000 -d120s --latency $TARGET/api/users/123 > ${OUTPUT_PREFIX}-5k-json.txt
cat ${OUTPUT_PREFIX}-5k-json.txt
echo

echo "========================================"
echo "BENCHMARK COMPLETE"
echo "========================================"
