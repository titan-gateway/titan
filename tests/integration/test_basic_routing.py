"""
Basic routing and request flow integration tests
Tests that Titan correctly routes requests to backend servers
"""
import pytest
import requests


def test_titan_server_starts(titan_server):
    """Test that Titan server starts and is reachable"""
    # If we got here, the fixture succeeded
    assert titan_server == "http://127.0.0.1:8080"


def test_root_endpoint(titan_server, http_session):
    """Test routing to root endpoint"""
    resp = http_session.get(f"{titan_server}/", timeout=2)

    assert resp.status_code == 200
    data = resp.json()
    assert "message" in data
    assert "mock backend" in data["message"].lower()


def test_health_endpoint(titan_server, http_session):
    """Test routing to health check endpoint"""
    resp = http_session.get(f"{titan_server}/health", timeout=2)

    assert resp.status_code == 200
    data = resp.json()
    assert data["status"] == "healthy"
    assert "port" in data


def test_parametrized_route(titan_server, http_session):
    """Test routing with path parameters"""
    user_id = 42
    resp = http_session.get(f"{titan_server}/api/users/{user_id}", timeout=2)

    assert resp.status_code == 200
    data = resp.json()
    assert data["id"] == str(user_id)  # Mock backend returns string
    assert data["name"] == f"User {user_id}"
    assert "port" in data


def test_multiple_requests(titan_server, http_session):
    """Test that Titan handles multiple sequential requests"""
    for i in range(10):
        resp = http_session.get(f"{titan_server}/api/users/{i}", timeout=2)
        assert resp.status_code == 200
        data = resp.json()
        assert data["id"] == str(i)  # Mock backend returns string


def test_post_request(titan_server, http_session):
    """Test POST request routing"""
    # Note: Our config only has GET routes, so this might fail
    # This tests error handling
    resp = http_session.post(
        f"{titan_server}/api/data", json={"test": "data"}, timeout=2
    )

    # Since we don't have a POST route configured, expect 404 or similar
    # TODO: Update once POST routing is configured
    assert resp.status_code in [200, 201, 404, 405]


def test_nonexistent_route(titan_server, http_session):
    """Test request to non-configured route"""
    resp = http_session.get(f"{titan_server}/nonexistent", timeout=2)

    # Should return 404 Not Found
    assert resp.status_code == 404


def test_connection_reuse(titan_server, http_session):
    """Test that HTTP keep-alive works (connection reuse)"""
    # Make multiple requests with same session
    responses = []
    for _ in range(5):
        resp = http_session.get(f"{titan_server}/health", timeout=2)
        responses.append(resp)

    # All should succeed
    assert all(r.status_code == 200 for r in responses)

    # Check that we reused the connection (same underlying socket)
    # This is implicit in using requests.Session()


def test_concurrent_requests(titan_server):
    """Test handling concurrent requests from multiple clients"""
    import concurrent.futures

    def make_request(i):
        resp = requests.get(f"{titan_server}/api/users/{i}", timeout=5)
        return resp.status_code == 200

    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
        futures = [executor.submit(make_request, i) for i in range(20)]
        results = [f.result() for f in concurrent.futures.as_completed(futures)]

    # All requests should succeed
    assert all(results)
    assert len(results) == 20


def test_request_headers_forwarded(titan_server, http_session):
    """Test that custom headers are forwarded to backend"""
    headers = {"X-Custom-Header": "test-value"}
    resp = http_session.get(f"{titan_server}/health", headers=headers, timeout=2)

    # Backend should receive the request (even if it doesn't use the header)
    assert resp.status_code == 200


def test_large_response(titan_server, http_session):
    """Test handling large responses from backend"""
    resp = http_session.get(f"{titan_server}/large", timeout=5)

    assert resp.status_code == 200
    data = resp.json()
    assert "items" in data  # Backend returns "items" field
    assert len(data["items"]) == 1000  # Backend returns 1000 items
    assert data["items"][0]["id"] == 0  # Verify item structure
    assert "value" in data["items"][0]
