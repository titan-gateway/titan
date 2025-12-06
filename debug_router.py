#!/usr/bin/env python3
"""Quick script to debug router matching"""
import subprocess
import time
import requests
import os

# Start Titan
env = os.environ.copy()
env['LD_PRELOAD'] = '/usr/lib/aarch64-linux-gnu/libasan.so.8'

proc = subprocess.Popen(
    ['/workspace/build/dev/src/titan', '--config', '/tmp/test_router_debug.json'],
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    bufsize=1,  # Line buffered
    env=env
)

# Wait for startup
time.sleep(2)

# Make request
print("=" * 80)
print("MAKING REQUEST TO /public")
print("=" * 80)
try:
    resp = requests.get('http://127.0.0.1:8080/public', timeout=2)
    print(f"Response status: {resp.status_code}")
    print(f"Response body: {resp.text}")
except Exception as e:
    print(f"Request failed: {e}")

# Wait a bit for logs
time.sleep(1)

# Kill and get output
proc.terminate()
output, _ = proc.communicate(timeout=3)

print("=" * 80)
print("TITAN OUTPUT:")
print("=" * 80)
print(output)
