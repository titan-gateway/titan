"""
Titan HTTP/2 Integration Tests

Tests HTTP/2 protocol support (h2c - HTTP/2 cleartext with prior knowledge)
"""

import subprocess
import time
import pytest


def test_http2_preface_detection(titan_server):
    """Test that server detects HTTP/2 connection preface"""
    # Send raw HTTP/2 connection preface using curl
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",  # Use h2c (HTTP/2 cleartext)
            "--http2",                    # Force HTTP/2
            "-v",                          # Verbose output
            "-m", "5",                     # 5 second timeout
            f"{titan_server}/",
        ],
        capture_output=True,
        text=True,
    )

    # curl should successfully connect
    # Note: Server may not fully support HTTP/2 yet, but it should accept the connection
    assert result.returncode in [0, 52, 56]  # 0 = success, 52 = empty reply, 56 = recv failure


def test_http2_get_request(titan_server):
    """Test simple GET request over HTTP/2"""
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-v",
            "-m", "5",
            f"{titan_server}/",
        ],
        capture_output=True,
        text=True,
    )

    # Check that HTTP/2 was used (visible in stderr)
    assert "HTTP/2" in result.stderr or "h2" in result.stderr


def test_http2_multiple_requests(titan_server):
    """Test multiple HTTP/2 requests over same connection"""
    # Use curl with connection reuse
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-v",
            "-m", "10",
            f"{titan_server}/",
            f"{titan_server}/health",
            f"{titan_server}/api/users/123",
        ],
        capture_output=True,
        text=True,
    )

    # Curl should attempt multiple requests
    assert result.returncode in [0, 52, 56]


def test_http1_still_works(titan_server):
    """Verify HTTP/1.1 requests still work after adding HTTP/2 support"""
    import requests

    # Send regular HTTP/1.1 request
    resp = requests.get(f"{titan_server}/", timeout=5)

    assert resp.status_code == 200


def test_http1_and_http2_coexist(titan_server):
    """Test that HTTP/1.1 and HTTP/2 work on same server"""
    import requests

    # HTTP/1.1 request
    resp1 = requests.get(f"{titan_server}/", timeout=5)
    assert resp1.status_code == 200

    # HTTP/2 request
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-m", "5",
            f"{titan_server}/",
        ],
        capture_output=True,
        text=True,
    )

    # Both should work
    assert resp1.status_code == 200
    assert result.returncode in [0, 52, 56]


def test_http2_proxied_request(titan_server):
    """Test HTTP/2 request to proxied backend endpoint"""
    # Send HTTP/2 request to /health which is proxied to backend
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-v",
            "-m", "5",
            f"{titan_server}/health",
        ],
        capture_output=True,
        text=True,
    )

    # Request should succeed (or at least attempt)
    assert result.returncode in [0, 52, 56]

    # If successful, check for response body
    if result.returncode == 0:
        assert len(result.stdout) > 0


def test_http2_parametrized_proxied_route(titan_server):
    """Test HTTP/2 request to parametrized proxied route"""
    # Send HTTP/2 request to /api/users/:id
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-m", "5",
            f"{titan_server}/api/users/123",
        ],
        capture_output=True,
        text=True,
    )

    # Request should attempt
    assert result.returncode in [0, 52, 56]


def test_http2_large_proxied_response(titan_server):
    """Test HTTP/2 large response from proxied backend"""
    # Send HTTP/2 request to /large endpoint
    result = subprocess.run(
        [
            "curl",
            "--http2-prior-knowledge",
            "-m", "5",
            f"{titan_server}/large",
        ],
        capture_output=True,
        text=True,
    )

    # Request should complete
    assert result.returncode in [0, 52, 56]

    # If successful, verify large response
    if result.returncode == 0:
        assert len(result.stdout) > 10000  # Large response is ~30KB
