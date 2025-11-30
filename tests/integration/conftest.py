"""
Pytest configuration and fixtures for Titan integration tests
"""
import json
import os
import signal
import subprocess
import time
from pathlib import Path
from typing import Generator

import psutil
import pytest
import requests


# Paths
REPO_ROOT = Path(__file__).parent.parent.parent
BUILD_DIR = REPO_ROOT / "build" / "dev"
TITAN_BINARY = BUILD_DIR / "src" / "titan"
INTEGRATION_TEST_DIR = Path(__file__).parent
TEST_CONFIG_DIR = Path(__file__).parent / "configs"


class ProcessManager:
    """Manages test processes (Titan, backends)"""

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

    def wait_for_port(self, port: int, timeout: float = 5.0, host: str = "127.0.0.1"):
        """Wait for a port to be listening"""
        import socket

        start = time.time()
        while time.time() - start < timeout:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.5)
                result = sock.connect_ex((host, port))
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
                    # Try graceful shutdown first
                    proc.terminate()
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    # Force kill if needed
                    proc.kill()
                    proc.wait()

        self.processes.clear()


@pytest.fixture(scope="session")
def process_manager():
    """Session-wide process manager"""
    pm = ProcessManager()
    yield pm
    pm.stop_all()


@pytest.fixture(scope="session")
def mock_backend_1(process_manager):
    """Start unified FastAPI mock backend on port 3001"""
    proc = process_manager.start_process(
        "backend-1",
        ["uvicorn", "main:app", "--host", "127.0.0.1", "--port", "3001", "--log-level", "warning"],
        cwd=INTEGRATION_TEST_DIR,
        env={**os.environ},
    )

    # Wait for backend to be ready
    if not process_manager.wait_for_port(3001, timeout=10):
        raise RuntimeError("Backend 1 failed to start on port 3001")

    # Verify it's responding
    try:
        resp = requests.get("http://127.0.0.1:3001/health", timeout=2)
        assert resp.status_code == 200
    except Exception as e:
        raise RuntimeError(f"Backend 1 health check failed: {e}")

    yield "http://127.0.0.1:3001"


@pytest.fixture(scope="session")
def mock_backend_2(process_manager):
    """Start unified FastAPI mock backend on port 3002"""
    proc = process_manager.start_process(
        "backend-2",
        ["uvicorn", "main:app", "--host", "127.0.0.1", "--port", "3002", "--log-level", "warning"],
        cwd=INTEGRATION_TEST_DIR,
        env={**os.environ},
    )

    if not process_manager.wait_for_port(3002, timeout=10):
        raise RuntimeError("Backend 2 failed to start on port 3002")

    try:
        resp = requests.get("http://127.0.0.1:3002/health", timeout=2)
        assert resp.status_code == 200
    except Exception as e:
        raise RuntimeError(f"Backend 2 health check failed: {e}")

    yield "http://127.0.0.1:3002"


@pytest.fixture
def titan_config(tmp_path, mock_backend_1, mock_backend_2):
    """Create a Titan configuration file for testing"""
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
                "name": "test_backend",
                "load_balancing": "round_robin",
                "backends": [
                    {"host": "127.0.0.1", "port": 3001, "weight": 1, "max_connections": 100},
                ],
            }
        ],
        "routes": [
            {"path": "/", "method": "GET", "handler_id": "root", "upstream": "test_backend", "priority": 10},
            {"path": "/health", "method": "GET", "handler_id": "health", "upstream": "test_backend", "priority": 10},
            {"path": "/api/users/:id", "method": "GET", "handler_id": "get_user", "upstream": "test_backend", "priority": 5},
            {"path": "/large", "method": "GET", "handler_id": "large", "upstream": "test_backend", "priority": 5},
            {"path": "/slow", "method": "GET", "handler_id": "slow", "upstream": "test_backend", "priority": 5},
        ],
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False, "requests_per_second": 1000, "burst_size": 2000},
        "auth": {"enabled": False},
        "logging": {"level": "info", "format": "text"},
        "metrics": {"enabled": True, "port": 9090, "path": "/metrics"},
    }

    config_path = tmp_path / "titan_test.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_server(process_manager, titan_config, mock_backend_1, mock_backend_2):
    """Start Titan server with test configuration"""
    # Ensure binary exists
    if not TITAN_BINARY.exists():
        raise RuntimeError(
            f"Titan binary not found at {TITAN_BINARY}. "
            "Run 'cmake --build build/dev' first."
        )

    # Start Titan
    proc = process_manager.start_process(
        "titan",
        [str(TITAN_BINARY), "--config", str(titan_config)],
        cwd=REPO_ROOT,
    )

    # Wait for Titan to be ready
    if not process_manager.wait_for_port(8080, timeout=5):
        # Print stderr if startup failed
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    # Give it more time to fully initialize and connect to backends
    time.sleep(2.0)

    yield "http://127.0.0.1:8080"

    # Cleanup happens via process_manager fixture


@pytest.fixture
def http_session():
    """HTTP session for making requests"""
    session = requests.Session()
    session.headers.update({"User-Agent": "Titan-Integration-Test/1.0"})
    yield session
    session.close()
