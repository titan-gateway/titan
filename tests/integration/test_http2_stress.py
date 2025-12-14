"""
Titan HTTP/2 Stress Tests

High-load correctness tests using h2load to verify HTTP/2 multiplexing,
connection reuse, and concurrency handling.

These tests catch bugs that traditional integration tests miss:
- TCP buffer stalls (EAGAIN handling)
- Stream lifecycle race conditions
- Connection reuse issues
- High concurrency timing bugs
"""

import subprocess
import re
import pytest


def parse_h2load_output(stdout: str) -> dict:
    """Parse h2load output to extract success/failure counts"""
    result = {
        "total": 0,
        "started": 0,
        "done": 0,
        "succeeded": 0,
        "failed": 0,
        "errored": 0,
        "timeout": 0,
    }

    # Look for line like: "requests: 100 total, 100 started, 100 done, 100 succeeded, 0 failed, 0 errored, 0 timeout"
    pattern = r"requests:\s+(\d+)\s+total,\s+(\d+)\s+started,\s+(\d+)\s+done,\s+(\d+)\s+succeeded,\s+(\d+)\s+failed,\s+(\d+)\s+errored,\s+(\d+)\s+timeout"
    match = re.search(pattern, stdout)

    if match:
        result["total"] = int(match.group(1))
        result["started"] = int(match.group(2))
        result["done"] = int(match.group(3))
        result["succeeded"] = int(match.group(4))
        result["failed"] = int(match.group(5))
        result["errored"] = int(match.group(6))
        result["timeout"] = int(match.group(7))

    return result


def test_http2_stream_multiplexing(titan_server_tls):
    """
    Test HTTP/2 concurrent stream multiplexing

    This test catches:
    - TCP buffer stall bugs (EAGAIN handling)
    - Stream interleaving issues
    - Frame processing race conditions

    Uses -m20 (20 max concurrent streams) to stress multiplexing.
    """
    result = subprocess.run(
        [
            "h2load",
            "-v",            # Verbose for debugging
            "-t1",           # 1 thread
            "-c1",           # 1 connection
            "-n100",         # 100 requests
            "-m20",          # 20 max concurrent streams (to trigger bug)
            titan_server_tls + "/",
        ],
        capture_output=True,
        text=True,
        timeout=30,
    )

    assert result.returncode == 0, f"h2load failed with exit code {result.returncode}"

    # Parse output
    stats = parse_h2load_output(result.stdout)

    # Print debug info if there are failures
    if stats["succeeded"] != 100:
        print("\n=== h2load STDOUT ===")
        print(result.stdout)
        print("\n=== h2load STDERR ===")
        print(result.stderr)

    # Verify 100% success rate
    assert stats["total"] == 100, f"Expected 100 total requests, got {stats['total']}"
    assert stats["succeeded"] == 100, f"Expected 100 succeeded, got {stats['succeeded']}"
    assert stats["failed"] == 0, f"Expected 0 failed, got {stats['failed']}"
    assert stats["errored"] == 0, f"Expected 0 errored, got {stats['errored']}"
    assert stats["timeout"] == 0, f"Expected 0 timeout, got {stats['timeout']}"


def test_http2_connection_reuse(titan_server_tls):
    """
    Test connection reuse with many requests

    This test catches:
    - Stream lifecycle bugs (cleanup timing)
    - Stream ID exhaustion/reuse issues
    - Memory leaks from unclosed streams

    Sends 1000 requests over 10 connections (100 per connection).
    """
    result = subprocess.run(
        [
            "h2load",
            "-t2",           # 2 threads
            "-c10",          # 10 connections
            "-n1000",        # 1000 requests (100 per connection)
            titan_server_tls + "/",
        ],
        capture_output=True,
        text=True,
        timeout=120,
    )

    assert result.returncode == 0, f"h2load failed: {result.stderr}"

    stats = parse_h2load_output(result.stdout)

    assert stats["total"] == 1000
    assert stats["succeeded"] == 1000
    assert stats["failed"] == 0
    assert stats["errored"] == 0


def test_http2_high_concurrency(titan_server_tls):
    """
    Test high concurrency stress

    This test catches:
    - Race conditions in connection handling
    - Epoll/event loop bugs under load
    - Thread safety issues (if applicable)

    Uses 50 concurrent connections with 5000 total requests.
    """
    result = subprocess.run(
        [
            "h2load",
            "-t4",           # 4 threads
            "-c50",          # 50 concurrent connections
            "-n5000",        # 5000 requests
            titan_server_tls + "/",
        ],
        capture_output=True,
        text=True,
        timeout=300,
    )

    assert result.returncode == 0, f"h2load failed: {result.stderr}"

    stats = parse_h2load_output(result.stdout)

    assert stats["total"] == 5000
    assert stats["succeeded"] == 5000
    assert stats["failed"] == 0
    assert stats["errored"] == 0


def test_http2_rapid_connection_cycling(titan_server_tls):
    """
    Test rapid connection open/close cycles

    This test catches:
    - Connection cleanup bugs
    - FD leaks
    - TLS handshake issues under load

    Uses many short-lived connections (1 request each).
    """
    result = subprocess.run(
        [
            "h2load",
            "-t4",
            "-c200",         # 200 connections
            "-n200",         # 200 requests (1 per connection)
            titan_server_tls + "/",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )

    assert result.returncode == 0, f"h2load failed: {result.stderr}"

    stats = parse_h2load_output(result.stdout)

    assert stats["total"] == 200
    assert stats["succeeded"] == 200
    assert stats["failed"] == 0
    assert stats["errored"] == 0
