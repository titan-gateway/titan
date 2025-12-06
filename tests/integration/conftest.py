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

    # Explicit cleanup to ensure Titan stops between tests
    if proc.poll() is None:
        print(f"Stopping Titan (PID {proc.pid})")
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    time.sleep(0.5)  # Ensure port 8080 is released


@pytest.fixture
def http_session():
    """HTTP session for making requests"""
    session = requests.Session()
    session.headers.update({"User-Agent": "Titan-Integration-Test/1.0"})
    yield session
    session.close()


# JWT Testing Fixtures


@pytest.fixture(scope="session")
def jwt_test_keys(tmp_path_factory):
    """
    Generate ephemeral JWT test keys (NOT stored in repo)
    Keys are created fresh for each test session and auto-deleted after

    This avoids security scanner false positives from keys in repo
    """
    from cryptography.hazmat.primitives.asymmetric import rsa, ec
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.backends import default_backend
    import os
    import base64

    keys_dir = tmp_path_factory.mktemp("jwt_keys")

    # Generate RSA 2048-bit key pair for RS256 (~50ms)
    rsa_private = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
        backend=default_backend()
    )

    rsa_private_pem = rsa_private.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption()
    )

    rsa_public_pem = rsa_private.public_key().public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    # Generate ECDSA P-256 key pair for ES256 (~10ms)
    ec_private = ec.generate_private_key(
        ec.SECP256R1(),
        backend=default_backend()
    )

    ec_private_pem = ec_private.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption()
    )

    ec_public_pem = ec_private.public_key().public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )

    # Generate HMAC secret for HS256
    hmac_secret = os.urandom(32)  # 256-bit secret

    # Write keys to temp directory
    rsa_private_path = keys_dir / "rsa_private.pem"
    rsa_public_path = keys_dir / "rsa_public.pem"
    ec_private_path = keys_dir / "ec_private.pem"
    ec_public_path = keys_dir / "ec_public.pem"

    rsa_private_path.write_bytes(rsa_private_pem)
    rsa_public_path.write_bytes(rsa_public_pem)
    ec_private_path.write_bytes(ec_private_pem)
    ec_public_path.write_bytes(ec_public_pem)

    return {
        "dir": keys_dir,
        "rsa_private_key": rsa_private,
        "rsa_private_path": rsa_private_path,
        "rsa_public_path": rsa_public_path,
        "rsa_private_pem": rsa_private_pem,
        "rsa_public_pem": rsa_public_pem,
        "ec_private_key": ec_private,
        "ec_private_path": ec_private_path,
        "ec_public_path": ec_public_path,
        "ec_private_pem": ec_private_pem,
        "ec_public_pem": ec_public_pem,
        "hmac_secret": base64.b64encode(hmac_secret).decode('ascii'),
    }


@pytest.fixture
def create_jwt_token(jwt_test_keys):
    """
    Factory to create JWT tokens with custom claims

    Usage:
        token = create_jwt_token(scopes=["read:users"], roles=["admin"])
        token = create_jwt_token(algorithm="ES256", exp_delta=3600)
    """
    import jwt
    import time

    def _create(
        algorithm="RS256",
        scopes=None,
        roles=None,
        exp_delta=3600,
        jti=None,
        **extra_claims
    ):
        """
        Create a JWT token for testing

        Args:
            algorithm: RS256, ES256, or HS256
            scopes: List of OAuth 2.0 scopes
            roles: List of RBAC roles
            exp_delta: Expiration time in seconds from now
            jti: JWT ID (for revocation testing)
            **extra_claims: Additional claims to include
        """
        now = int(time.time())

        payload = {
            "iss": "https://test.auth.titan.com",
            "sub": "test-user-123",
            "aud": "titan-api",
            "iat": now,
            "exp": now + exp_delta,
            "nbf": now - 60,  # Valid from 1 minute ago
        }

        # Add scopes and roles
        if scopes:
            payload["scope"] = " ".join(scopes)
        if roles:
            payload["roles"] = " ".join(roles)
        if jti:
            payload["jti"] = jti

        # Add any extra claims
        payload.update(extra_claims)

        # Choose signing key and kid based on algorithm
        import base64
        if algorithm == "RS256":
            signing_key = jwt_test_keys["rsa_private_pem"]
            kid = "test-rsa-key"
        elif algorithm == "ES256":
            signing_key = jwt_test_keys["ec_private_pem"]
            kid = "test-ec-key"
        elif algorithm == "HS256":
            # Decode base64-encoded HMAC secret
            signing_key = base64.b64decode(jwt_test_keys["hmac_secret"])
            kid = "test-hmac-key"
        else:
            raise ValueError(f"Unsupported algorithm: {algorithm}")

        return jwt.encode(payload, signing_key, algorithm=algorithm, headers={"kid": kid})

    return _create
