"""
Integration tests for JWT Authentication

Tests end-to-end authentication flow:
  Client → Titan (JWT signature validation, claims validation) → Backend
           ↓
        200 OK / 401 Unauthorized
"""
import json
import pytest
import requests
import time


@pytest.fixture
def titan_jwt_auth_config(tmp_path, jwt_test_keys, mock_backend_1):
    """Titan config with JWT authentication enabled (minimal config)"""
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
            }
        ],
        "routes": [
            # Public route
            {
                "path": "/public",
                "method": "GET",
                "handler_id": "public",
                "upstream": "backend",
                "priority": 10,
                "auth_required": False,
            },
            # Protected route
            {
                "path": "/protected",
                "method": "GET",
                "handler_id": "protected",
                "upstream": "backend",
                "priority": 10,
                "auth_required": True,
            },
            # API route
            {
                "path": "/api/users/:id",
                "method": "GET",
                "handler_id": "get_user",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
            },
        ],
        "jwt": {
            "enabled": True,
            "header": "Authorization",
            "scheme": "Bearer",
            "keys": [
                # RS256 key
                {
                    "algorithm": "RS256",
                    "key_id": "test-rsa-key",
                    "public_key_path": str(jwt_test_keys["rsa_public_path"]),
                },
                # ES256 key
                {
                    "algorithm": "ES256",
                    "key_id": "test-ec-key",
                    "public_key_path": str(jwt_test_keys["ec_public_path"]),
                },
                # HS256 key
                {
                    "algorithm": "HS256",
                    "key_id": "test-hmac-key",
                    "secret": jwt_test_keys["hmac_secret"],
                },
            ],
            "allowed_issuers": ["https://test.auth.titan.com"],
            "allowed_audiences": ["titan-api"],
            "require_exp": True,
            "require_sub": False,
            "clock_skew_seconds": 60,
            "cache_enabled": True,
            "cache_capacity": 10000,
        },
        "jwt_authz": {"enabled": False},  # Disable authorization (only testing authentication)
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False},
        "logging": {"level": "info", "format": "text"},
        "metrics": {"enabled": False},
    }

    config_path = tmp_path / "titan_jwt_auth.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_jwt_auth_server(process_manager, titan_jwt_auth_config, mock_backend_1, jwt_test_keys):
    """Start Titan server with JWT authentication enabled"""
    from pathlib import Path
    import time

    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build" / "dev" / "src" / "titan"

    if not TITAN_BINARY.exists():
        raise RuntimeError(f"Titan binary not found at {TITAN_BINARY}")

    proc = process_manager.start_process(
        "titan-jwt-auth",
        [str(TITAN_BINARY), "--config", str(titan_jwt_auth_config)],
        cwd=REPO_ROOT,
    )

    if not process_manager.wait_for_port(8080, timeout=5):
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    time.sleep(2.0)

    # Print Titan output for debugging
    import select
    if proc.stdout and select.select([proc.stdout], [], [], 0.1)[0]:
        startup_output = proc.stdout.read1(4096).decode('utf-8', errors='ignore')
        print(f"[DEBUG] Titan startup output:\n{startup_output}")

    yield "http://127.0.0.1:8080"

    # Capture final output before shutdown
    if proc.poll() is None:
        proc.terminate()
        try:
            stdout, stderr = proc.communicate(timeout=3)
            if stdout:
                print(f"[DEBUG] Titan final stdout:\n{stdout}")
            if stderr:
                print(f"[DEBUG] Titan final stderr:\n{stderr}")
        except:
            proc.kill()
            proc.wait()
    time.sleep(0.5)


# Public Route Tests


def test_public_route_without_token(titan_jwt_auth_server):
    """Public routes should work without JWT token"""
    resp = requests.get(f"{titan_jwt_auth_server}/public")
    assert resp.status_code == 200


def test_public_route_with_token(titan_jwt_auth_server, create_jwt_token):
    """Public routes should work with valid JWT token"""
    token = create_jwt_token()
    resp = requests.get(
        f"{titan_jwt_auth_server}/public",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


# Valid Token Tests


def test_valid_rs256_token(titan_jwt_auth_server, create_jwt_token):
    """Valid RS256 token should be accepted"""
    token = create_jwt_token(algorithm="RS256")
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_valid_es256_token(titan_jwt_auth_server, create_jwt_token):
    """Valid ES256 token should be accepted"""
    token = create_jwt_token(algorithm="ES256")
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_valid_hs256_token(titan_jwt_auth_server, create_jwt_token):
    """Valid HS256 token should be accepted"""
    token = create_jwt_token(algorithm="HS256")
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


# Missing Token Tests


def test_missing_authorization_header(titan_jwt_auth_server):
    """Request without Authorization header should return 401"""
    resp = requests.get(f"{titan_jwt_auth_server}/protected")
    assert resp.status_code == 401
    assert resp.json()["error"] == "unauthorized"


def test_empty_authorization_header(titan_jwt_auth_server):
    """Request with empty Authorization header should return 401"""
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": ""}
    )
    assert resp.status_code == 401


def test_malformed_authorization_header(titan_jwt_auth_server):
    """Request with malformed Authorization header should return 401"""
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": "NotBearer token123"}
    )
    assert resp.status_code == 401


# Invalid Token Tests


def test_invalid_token_format(titan_jwt_auth_server):
    """Malformed JWT token should return 401"""
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": "Bearer not-a-jwt-token"}
    )
    assert resp.status_code == 401


def test_invalid_signature(titan_jwt_auth_server, create_jwt_token):
    """Token with invalid signature should return 401"""
    import jwt

    # Create token with wrong key
    wrong_payload = {
        "iss": "https://test.auth.titan.com",
        "sub": "test-user",
        "aud": "titan-api",
        "exp": int(time.time()) + 3600,
    }
    wrong_token = jwt.encode(wrong_payload, "wrong-secret-key", algorithm="HS256")

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {wrong_token}"}
    )
    assert resp.status_code == 401


# Expiration Tests


def test_expired_token(titan_jwt_auth_server, create_jwt_token):
    """Expired JWT token should return 401"""
    # Token expired 1 hour ago
    token = create_jwt_token(exp_delta=-3600)
    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 401
    # Note: Error message should mention "expired" but we just check 401


def test_token_not_yet_valid(titan_jwt_auth_server, jwt_test_keys):
    """Token with nbf (not before) in future should return 401"""
    import jwt

    now = int(time.time())
    payload = {
        "iss": "https://test.auth.titan.com",
        "sub": "test-user",
        "aud": "titan-api",
        "exp": now + 7200,  # Expires in 2 hours
        "nbf": now + 3600,  # Not valid for 1 hour
        "iat": now,
    }

    token = jwt.encode(payload, jwt_test_keys["rsa_private_pem"], algorithm="RS256")

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 401


# Issuer/Audience Validation


def test_wrong_issuer(titan_jwt_auth_server, jwt_test_keys):
    """Token from wrong issuer should return 401"""
    import jwt

    payload = {
        "iss": "https://wrong.issuer.com",  # Wrong issuer
        "sub": "test-user",
        "aud": "titan-api",
        "exp": int(time.time()) + 3600,
    }

    token = jwt.encode(payload, jwt_test_keys["rsa_private_pem"], algorithm="RS256")

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 401


def test_wrong_audience(titan_jwt_auth_server, jwt_test_keys):
    """Token for wrong audience should return 401"""
    import jwt

    payload = {
        "iss": "https://test.auth.titan.com",
        "sub": "test-user",
        "aud": "wrong-audience",  # Wrong audience
        "exp": int(time.time()) + 3600,
    }

    token = jwt.encode(payload, jwt_test_keys["rsa_private_pem"], algorithm="RS256")

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 401


# Claims Extraction Tests


def test_claims_available_to_backend(titan_jwt_auth_server, create_jwt_token):
    """JWT claims should be extractable (though backend doesn't see them in this test)"""
    token = create_jwt_token(
        scopes=["read:users", "write:posts"],
        roles=["admin"],
        sub="user-12345",
    )

    resp = requests.get(
        f"{titan_jwt_auth_server}/api/users/123",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200
    # Note: We can't verify claim extraction without backend support
    # This just verifies the token is accepted


# Token Caching Tests


def test_token_caching(titan_jwt_auth_server, create_jwt_token):
    """Same token should hit cache on subsequent requests"""
    token = create_jwt_token()

    # First request (cache miss)
    resp1 = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp1.status_code == 200

    # Second request (should hit cache)
    resp2 = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp2.status_code == 200

    # Third request (should also hit cache)
    resp3 = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp3.status_code == 200


# Edge Cases


def test_token_with_extra_claims(titan_jwt_auth_server, create_jwt_token):
    """Token with custom claims should be accepted"""
    token = create_jwt_token(
        custom_claim="custom_value",
        another_claim=12345,
    )

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_token_without_subject_claim(titan_jwt_auth_server, jwt_test_keys):
    """Token without 'sub' claim should be accepted (require_sub=false)"""
    import jwt

    payload = {
        "iss": "https://test.auth.titan.com",
        "aud": "titan-api",
        "exp": int(time.time()) + 3600,
        # No 'sub' claim
    }

    token = jwt.encode(payload, jwt_test_keys["rsa_private_pem"], algorithm="RS256")

    resp = requests.get(
        f"{titan_jwt_auth_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200
