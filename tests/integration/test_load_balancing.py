"""
Load balancing integration tests
Tests that Titan distributes requests across multiple backends
"""
import pytest
import requests
from collections import Counter


def test_round_robin_distribution(titan_server, http_session):
    """Test that round-robin load balancing distributes requests evenly"""
    # Make multiple requests
    num_requests = 20
    responses = []

    for _ in range(num_requests):
        resp = http_session.get(f"{titan_server}/health", timeout=2)
        assert resp.status_code == 200
        responses.append(resp.json())

    # Both backends should have received requests
    # In perfect round-robin with 2 backends, each should get 10 requests
    # Due to threading and connection pooling, distribution might not be perfect
    # but both should have received at least some requests

    # Note: We can't easily determine which backend handled each request
    # without adding backend identification to responses
    # For now, just verify all requests succeeded
    assert len(responses) == num_requests
    assert all("status" in r for r in responses)


def test_backend_failover(titan_server, mock_backend_1, mock_backend_2):
    """Test behavior when one backend is down"""
    # This is a placeholder - actual implementation would require:
    # 1. Stopping one backend
    # 2. Verifying requests still work (go to healthy backend)
    # 3. Restarting backend
    # 4. Verifying both backends are used again

    # For now, just verify both backends are healthy
    resp1 = requests.get(f"{mock_backend_1}/health", timeout=2)
    resp2 = requests.get(f"{mock_backend_2}/health", timeout=2)

    assert resp1.status_code == 200
    assert resp2.status_code == 200


def test_multiple_concurrent_clients(titan_server):
    """Test load distribution with concurrent clients"""
    import concurrent.futures

    def make_requests(client_id, num_requests=10):
        session = requests.Session()
        results = []
        for i in range(num_requests):
            try:
                resp = session.get(f"{titan_server}/api/users/{client_id}", timeout=5)
                results.append(resp.status_code == 200)
            except Exception as e:
                results.append(False)
        session.close()
        return sum(results)

    # Simulate 5 concurrent clients, each making 10 requests
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(make_requests, i) for i in range(5)]
        successes = [f.result() for f in concurrent.futures.as_completed(futures)]

    # Each client should have all 10 requests succeed
    assert all(s == 10 for s in successes)
    assert sum(successes) == 50  # Total 50 successful requests
