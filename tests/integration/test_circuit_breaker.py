"""
Integration test for circuit breaker functionality
Tests failure detection, circuit opening/closing, and metrics
"""
import json
import os
import subprocess
import time
from pathlib import Path

import pytest
import requests


# Paths
REPO_ROOT = Path(__file__).parent.parent.parent
BUILD_DIR = REPO_ROOT / "build" / "dev"
TITAN_BINARY = BUILD_DIR / "src" / "titan"
INTEGRATION_TEST_DIR = Path(__file__).parent


class ProcessManager:
    """Manages test processes"""

    def __init__(self):
        self.processes = []

    def start_process(self, name: str, cmd: list, cwd=None, env=None) -> subprocess.Popen:
        """Start a process and track it"""
        print(f"Starting {name}: {' '.join(cmd)}")
        proc = subprocess.Popen(
            cmd,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.processes.append((name, proc))
        return proc

    def wait_for_port(self, port: int, timeout: float = 5.0):
        """Wait for a port to be listening"""
        import socket

        start = time.time()
        while time.time() - start < timeout:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.5)
                result = sock.connect_ex(("127.0.0.1", port))
                sock.close()
                if result == 0:
                    return True
            except Exception:
                pass
            time.sleep(0.1)
        return False

    def stop_all(self):
        """Stop all managed processes"""
        for name, proc in self.processes:
            if proc.poll() is None:  # Still running
                print(f"Stopping {name} (PID {proc.pid})")
                try:
                    proc.terminate()
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
        self.processes.clear()


@pytest.fixture
def process_manager():
    """Process manager fixture"""
    # Kill any stale processes from previous tests
    import os
    import signal
    try:
        os.system("pkill -9 -f 'uvicorn.*3001' 2>/dev/null")
        os.system("pkill -9 -f 'titan.*8080' 2>/dev/null")
    except:
        pass
    time.sleep(1.0)  # Longer cleanup delay

    pm = ProcessManager()
    yield pm
    pm.stop_all()
    # Longer delay to ensure ports are released for next test
    time.sleep(1.5)


@pytest.fixture
def mock_backend(process_manager):
    """Start unified FastAPI mock backend with circuit breaker control endpoints"""
    proc = process_manager.start_process(
        "mock-backend",
        ["uvicorn", "main:app", "--host", "127.0.0.1", "--port", "3001", "--log-level", "warning"],
        cwd=INTEGRATION_TEST_DIR,
    )

    if not process_manager.wait_for_port(3001, timeout=10):
        raise RuntimeError("Mock backend failed to start on port 3001")

    # Verify it's responding
    try:
        resp = requests.get("http://127.0.0.1:3001/health", timeout=2)
        assert resp.status_code == 200
    except Exception as e:
        raise RuntimeError(f"Mock backend health check failed: {e}")

    yield "http://127.0.0.1:3001"


@pytest.fixture
def titan_config_with_circuit_breaker(tmp_path):
    """Create Titan config with circuit breaker enabled"""
    config = {
        "version": "1.0",
        "server": {
            "worker_threads": 1,
            "listen_address": "127.0.0.1",
            "listen_port": 8080,
            "backlog": 128,
        },
        "upstreams": [
            {
                "name": "backend",
                "load_balancing": "round_robin",
                "backends": [
                    {"host": "127.0.0.1", "port": 3001, "weight": 1, "max_connections": 100},
                ],
                "circuit_breaker": {
                    "enabled": True,
                    "failure_threshold": 3,  # Open after 3 failures
                    "success_threshold": 2,  # Close after 2 successes in HALF_OPEN
                    "timeout_ms": 5000,  # 5 seconds before trying again
                    "window_ms": 10000,  # 10 second failure window
                    "enable_global_hints": True,
                    "catastrophic_threshold": 10,
                },
            }
        ],
        "routes": [
            {"path": "/api", "method": "GET", "handler_id": "api", "upstream": "backend", "priority": 10},
        ],
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False, "requests_per_second": 1000, "burst_size": 2000},
        "auth": {"enabled": False},
        "logging": {"level": "info", "format": "text"},
        "metrics": {"enabled": True, "port": 9090, "path": "/metrics"},
    }

    config_path = tmp_path / "titan_circuit_breaker_test.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_server(process_manager, titan_config_with_circuit_breaker, mock_backend):
    """Start Titan server with circuit breaker config"""
    if not TITAN_BINARY.exists():
        raise RuntimeError(
            f"Titan binary not found at {TITAN_BINARY}. Run 'cmake --build build/dev' first."
        )

    proc = process_manager.start_process(
        "titan",
        [str(TITAN_BINARY), "--config", str(titan_config_with_circuit_breaker)],
        cwd=REPO_ROOT,
    )

    if not process_manager.wait_for_port(8080, timeout=10):
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    # Wait for full initialization
    time.sleep(3.0)  # Longer startup delay

    # Verify Titan is actually responding before yielding
    max_retries = 10
    for attempt in range(max_retries):
        try:
            resp = requests.get("http://127.0.0.1:9090/health", timeout=2)
            if resp.status_code == 200:
                # Also verify the API route is available
                try:
                    api_resp = requests.get("http://127.0.0.1:8080/api", timeout=2)
                    # Should get 200 from mock backend (not 404)
                    if api_resp.status_code in [200, 500]:  # Either backend success or failure, but not 404
                        print(f"[FIXTURE] Titan fully ready (API route responding with {api_resp.status_code})")
                        break
                except:
                    pass
        except:
            pass

        if attempt < max_retries - 1:
            print(f"[FIXTURE] Waiting for Titan to be ready (attempt {attempt+1}/{max_retries})...")
            time.sleep(1.0)
        else:
            raise RuntimeError("Titan health check failed after startup")

    yield "http://127.0.0.1:8080"


@pytest.fixture
def metrics_url():
    """Metrics server URL (separate admin port)"""
    return "http://127.0.0.1:9090"


def test_circuit_breaker_metrics_accuracy(titan_server, mock_backend, metrics_url):
    """
    Test that circuit breaker metrics accurately track failures, successes, and state transitions
    """
    print(f"\n[TEST] Starting metrics accuracy test")
    print(f"[TEST] Titan server: {titan_server}")
    print(f"[TEST] Mock backend: {mock_backend}")
    print(f"[TEST] Metrics URL: {metrics_url}")

    # Reset backend state to ensure clean test
    try:
        requests.post(f"{mock_backend}/_control/reset", timeout=2)
        time.sleep(0.5)
    except:
        pass

    # Verify backend is responding
    print(f"[TEST] Checking backend health...")
    resp = requests.get(f"{mock_backend}/health", timeout=2)
    print(f"[TEST] Backend health: {resp.status_code}")
    assert resp.status_code == 200

    # Verify Titan is responding
    print(f"[TEST] Checking Titan health...")
    resp = requests.get(f"{metrics_url}/health", timeout=2)
    print(f"[TEST] Titan health: {resp.status_code}")
    assert resp.status_code == 200

    # 1. Send successful requests (backend starts in healthy state)
    success_count = 3
    for i in range(success_count):
        print(f"[TEST] Sending success request {i+1}/{success_count} to {titan_server}/api")
        # Add retry logic for startup timing
        for retry in range(3):
            try:
                resp = requests.get(f"{titan_server}/api", timeout=5)
                print(f"[TEST] Got response: {resp.status_code}")
                assert resp.status_code == 200
                break
            except AssertionError:
                if retry < 2:
                    print(f"[TEST] Retry {retry+1}/3 due to unexpected status")
                    time.sleep(0.5)
                else:
                    raise
    time.sleep(0.5)

    # 2. Check metrics show successes
    print(f"[TEST] Fetching metrics...")
    metrics = requests.get(f"{metrics_url}/metrics", timeout=5).text
    print(f"[TEST] Metrics length: {len(metrics)} bytes")
    assert "titan_circuit_breaker_successes_total" in metrics

    # 3. Trigger failures
    print(f"[TEST] Triggering backend failures...")
    resp = requests.post(f"{mock_backend}/_control/fail", timeout=5)
    print(f"[TEST] Backend fail response: {resp.status_code}")
    assert resp.status_code == 200

    for i in range(5):
        print(f"[TEST] Sending failure request {i+1}/5...")
        try:
            resp = requests.get(f"{titan_server}/api", timeout=5)
            print(f"[TEST] Got response: {resp.status_code}")
        except Exception as e:
            print(f"[TEST] Request failed (expected): {type(e).__name__}")
        time.sleep(0.1)
    time.sleep(0.5)

    # 4. Check metrics show failures and state transition
    print(f"[TEST] Fetching metrics after failures...")
    metrics = requests.get(f"{metrics_url}/metrics", timeout=5).text
    print(f"[TEST] Metrics length: {len(metrics)} bytes")
    assert "titan_circuit_breaker_failures_total" in metrics
    assert "titan_circuit_breaker_transitions_total" in metrics

    # Verify metrics have non-zero values
    lines = metrics.split("\n")
    for line in lines:
        if "titan_circuit_breaker_successes_total" in line and "backend=" in line:
            # Extract value
            value = int(line.split()[-1])
            print(f"[TEST] Successes: {value}")
            assert value >= success_count, f"Expected at least {success_count} successes, got {value}"

        if "titan_circuit_breaker_failures_total" in line and "backend=" in line:
            value = int(line.split()[-1])
            print(f"[TEST] Failures: {value}")
            assert value >= 3, f"Expected at least 3 failures, got {value}"

    print(f"[TEST] Test completed successfully")


def test_circuit_breaker_opens_on_failures(titan_server, mock_backend, metrics_url):
    """
    Test that circuit breaker opens after threshold failures
    """
    print(f"\n[TEST] Starting circuit breaker open test")

    # Reset backend state to ensure clean test
    try:
        requests.post(f"{mock_backend}/_control/reset", timeout=2)
        time.sleep(0.5)
    except:
        pass

    # 1. Verify normal operation
    for retry in range(3):
        try:
            resp = requests.get(f"{titan_server}/api", timeout=2)
            print(f"[TEST] Initial request status: {resp.status_code}")
            assert resp.status_code == 200
            assert resp.json()["message"] == "Success"
            break
        except (AssertionError, requests.exceptions.RequestException) as e:
            if retry < 2:
                print(f"[TEST] Retry {retry+1}/3 for initial request: {e}")
                time.sleep(0.5)
            else:
                raise

    # 2. Check initial circuit breaker state (CLOSED)
    metrics = requests.get(f"{metrics_url}/metrics", timeout=2).text
    assert "titan_circuit_breaker_state{" in metrics
    # State should be 0 (CLOSED) - backend is "127.0.0.1:3001" (localhost resolved to IP)
    assert 'titan_circuit_breaker_state{backend="127.0.0.1:3001",upstream="backend",worker="0"} 0' in metrics

    # 3. Trigger backend failures
    resp = requests.post(f"{mock_backend}/_control/fail", timeout=2)
    assert resp.status_code == 200
    time.sleep(0.3)  # Give backend time to update state

    # 4. Send requests to trigger circuit breaker (need 3 failures based on config)
    # Send enough requests to ensure we hit the threshold
    print(f"[TEST] Sending failure requests...")
    failed_requests = 0
    for i in range(10):
        try:
            resp = requests.get(f"{titan_server}/api", timeout=2)
            print(f"[TEST] Request {i+1} status: {resp.status_code}")
            if resp.status_code >= 500:
                failed_requests += 1
        except requests.exceptions.RequestException as e:
            print(f"[TEST] Request {i+1} exception: {type(e).__name__}")
            failed_requests += 1
        time.sleep(0.2)  # Slightly longer delay between requests

    # Should have at least 3 failures
    print(f"[TEST] Failed requests: {failed_requests}")
    assert failed_requests >= 3, f"Expected at least 3 failures, got {failed_requests}"

    # 5. Wait a moment for circuit breaker to open
    time.sleep(1.0)  # Longer wait for state transition

    # 6. Verify circuit breaker is now OPEN (state = 1)
    metrics = requests.get(f"{metrics_url}/metrics", timeout=2).text
    print(f"[TEST] Checking circuit breaker state...")
    assert "titan_circuit_breaker_failures_total" in metrics
    # Check that state transitioned to OPEN - backend is "127.0.0.1:3001" (localhost resolved to IP)
    assert ('titan_circuit_breaker_state{backend="127.0.0.1:3001",upstream="backend",worker="0"} 1' in metrics or
            'titan_circuit_breaker_state{backend="127.0.0.1:3001",upstream="backend",worker="0"} 2' in metrics), \
        f"Circuit breaker should be OPEN or HALF_OPEN, got state 0 (CLOSED). Failed requests: {failed_requests}"


def test_circuit_breaker_closes_after_recovery(titan_server, mock_backend, metrics_url):
    """
    Test that circuit breaker closes after backend recovers
    """
    print(f"\n[TEST] Starting circuit breaker recovery test")

    # Reset backend state to ensure clean test
    try:
        requests.post(f"{mock_backend}/_control/reset", timeout=2)
        time.sleep(0.5)
    except:
        pass

    # 1. Make backend fail
    resp = requests.post(f"{mock_backend}/_control/fail", timeout=2)
    print(f"[TEST] Backend fail response: {resp.status_code}")
    time.sleep(0.3)

    # 2. Trigger circuit breaker to open (3+ failures)
    for i in range(5):
        try:
            requests.get(f"{titan_server}/api", timeout=2)
        except:
            pass
    time.sleep(0.5)

    # 3. Restore backend health
    resp = requests.post(f"{mock_backend}/_control/succeed", timeout=2)
    assert resp.status_code == 200

    # 4. Wait for circuit breaker timeout (configured as 5 seconds)
    print("Waiting for circuit breaker timeout (5s)...")
    time.sleep(6.0)

    # 5. Circuit should now be in HALF_OPEN, try requests to trigger transition to CLOSED
    success_count = 0
    for i in range(5):
        try:
            resp = requests.get(f"{titan_server}/api", timeout=2)
            if resp.status_code == 200:
                success_count += 1
        except:
            pass
        time.sleep(0.2)

    # 6. Verify circuit breaker is now CLOSED (state = 0)
    time.sleep(1.0)  # Wait for metrics to update
    metrics = requests.get(f"{metrics_url}/metrics", timeout=2).text

    # Should have successes recorded
    assert "titan_circuit_breaker_successes_total" in metrics
    # Should be back to CLOSED after successful recovery - backend is "127.0.0.1:3001" (localhost resolved to IP)
    assert 'titan_circuit_breaker_state{backend="127.0.0.1:3001",upstream="backend",worker="0"} 0' in metrics, \
        f"Circuit breaker should be CLOSED after recovery, metrics: {metrics}"
