"""
Integration tests for per-route rate limiting.

Tests multi-tier rate limiting with different limits per route:
- Public API: 10 req/s (burst 20)
- Premium API: 100 req/s (burst 200)
- Admin API: 1000 req/s (burst 2000)
"""

import pytest
import requests
import time
from pathlib import Path


@pytest.fixture
def per_route_config(tmp_path, mock_backend_1):
    """Create config with per-route rate limiting."""
    import json

    config = {
        "server": {
            "worker_threads": 4,
            "listen_address": "0.0.0.0",
            "listen_port": 8080,
            "backlog": 128,
            "read_timeout": 60000,
            "write_timeout": 60000,
            "max_connections": 10000,
            "enable_tls": False
        },
        "routes": [
            {
                "path": "/api/public/*",
                "method": "GET",
                "upstream": "backend",
                "middleware": ["public_rate_limit"]
            },
            {
                "path": "/api/premium/*",
                "method": "GET",
                "upstream": "backend",
                "middleware": ["premium_rate_limit"]
            },
            {
                "path": "/api/admin/*",
                "method": "GET",
                "upstream": "backend",
                "middleware": ["admin_rate_limit"]
            },
            {
                "path": "/health",
                "method": "GET",
                "upstream": "backend"
            }
        ],
        "upstreams": [
            {
                "name": "backend",
                "backends": [
                    {
                        "host": "127.0.0.1",
                        "port": 3001,
                        "weight": 1,
                        "max_connections": 1000
                    }
                ],
                "load_balancing": "round_robin"
            }
        ],
        "rate_limits": {
            "public_rate_limit": {
                "enabled": True,
                "requests_per_second": 10,
                "burst_size": 20
            },
            "premium_rate_limit": {
                "enabled": True,
                "requests_per_second": 100,
                "burst_size": 200
            },
            "admin_rate_limit": {
                "enabled": True,
                "requests_per_second": 1000,
                "burst_size": 2000
            }
        },
        "cors": {
            "enabled": False
        },
        "logging": {
            "level": "info",
            "format": "json",
            "output": str(tmp_path / "titan.log")
        },
        "metrics": {
            "enabled": False
        }
    }

    config_path = tmp_path / "per_route_test.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_server_with_config(process_manager, per_route_config, mock_backend_1):
    """Start Titan server with per-route rate limiting config"""
    import time

    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build" / "dev" / "src" / "titan"

    if not TITAN_BINARY.exists():
        raise RuntimeError(f"Titan binary not found at {TITAN_BINARY}")

    proc = process_manager.start_process(
        "titan-per-route",
        [str(TITAN_BINARY), "--config", str(per_route_config)],
        cwd=REPO_ROOT,
    )

    if not process_manager.wait_for_port(8080, timeout=5):
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    time.sleep(2.0)

    yield "http://127.0.0.1:8080"

    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except:
            proc.kill()
            proc.wait()
    time.sleep(0.5)


def test_public_api_rate_limit_enforced(titan_server_with_config, per_route_config):
    """Test that public API enforces 10 req/s limit.

    Config specifies burst_size=20, which is divided among 4 workers (20/4=5 per worker).
    SO_REUSEPORT distributes connections, so we need to exceed total capacity.
    """
    # Send 25 requests (exceeds configured burst_size=20)
    responses = []
    for i in range(25):
        resp = requests.get("http://localhost:8080/api/public/data")
        responses.append(resp.status_code)

    # First ~20 should succeed (configured burst), remaining should be rate limited
    success_count = responses.count(200)
    rate_limited_count = responses.count(429)

    assert success_count <= 20, f"Expected <= 20 successful requests, got {success_count}"
    assert rate_limited_count >= 3, f"Expected at least 3 rate limited requests, got {rate_limited_count}"


def test_premium_api_higher_limit(titan_server_with_config, per_route_config):
    """Test that premium API has higher limit (100 req/s)."""
    # Burst of 150 requests (limit is 100 req/s, burst 200)
    responses = []
    for i in range(150):
        resp = requests.get("http://localhost:8080/api/premium/data")
        responses.append(resp.status_code)

    # Should handle much more than public API
    success_count = responses.count(200)

    assert success_count >= 100, f"Expected >= 100 successful requests for premium, got {success_count}"


def test_admin_api_highest_limit(titan_server_with_config, per_route_config):
    """Test that admin API has highest limit (1000 req/s)."""
    # Burst of 500 requests (limit is 1000 req/s, burst 2000)
    responses = []
    for i in range(500):
        resp = requests.get("http://localhost:8080/api/admin/operations")
        responses.append(resp.status_code)

    # Should handle significantly more than premium API
    success_count = responses.count(200)

    assert success_count >= 400, f"Expected >= 400 successful requests for admin, got {success_count}"


def test_health_endpoint_no_rate_limit(titan_server_with_config, per_route_config):
    """Test that health endpoint has no rate limit (no middleware specified)."""
    # Send many requests to health endpoint
    responses = []
    for i in range(100):
        resp = requests.get("http://localhost:8080/health")
        responses.append(resp.status_code)

    # All should succeed - no rate limiting
    success_count = responses.count(200)

    assert success_count == 100, f"Expected 100 successful requests to health, got {success_count}"


def test_different_routes_independent_limits(titan_server_with_config, per_route_config):
    """Test that rate limits are independent per route."""
    # Exhaust public API limit
    for i in range(25):
        requests.get("http://localhost:8080/api/public/data")

    # Premium API should still work
    resp = requests.get("http://localhost:8080/api/premium/data")
    assert resp.status_code == 200, "Premium API should be independent of public API rate limit"

    # Admin API should still work
    resp = requests.get("http://localhost:8080/api/admin/operations")
    assert resp.status_code == 200, "Admin API should be independent of public API rate limit"


def test_named_middleware_isolation(titan_server_with_config, per_route_config):
    """Test that named middleware instances are isolated (separate token buckets)."""
    # Send 20 requests to public API (should exhaust burst)
    public_responses = []
    for i in range(20):
        resp = requests.get("http://localhost:8080/api/public/data")
        public_responses.append(resp.status_code)

    # Send 200 requests to premium API (should use different bucket)
    premium_responses = []
    for i in range(200):
        resp = requests.get("http://localhost:8080/api/premium/data")
        premium_responses.append(resp.status_code)

    public_success = public_responses.count(200)
    premium_success = premium_responses.count(200)

    # Public should be limited to ~20
    assert public_success <= 20, f"Public API burst should be limited, got {public_success}"

    # Premium should handle many more
    assert premium_success >= 150, f"Premium API should handle more, got {premium_success}"
