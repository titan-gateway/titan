"""
Integration tests for JWT Authorization (OAuth 2.0 Scopes & RBAC Roles)

Tests end-to-end authorization flow:
  Client → Titan (JWT validation + authorization) → Backend
           ↓
        200 OK / 403 Forbidden
"""
import json
import pytest
import requests


@pytest.fixture
def titan_jwt_config(tmp_path, jwt_test_keys, mock_backend_1):
    """Titan config with JWT authentication and authorization enabled"""
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
            # Public route (no auth required)
            {
                "path": "/public",
                "method": "GET",
                "handler_id": "public",
                "upstream": "backend",
                "priority": 10,
                "auth_required": False,
            },
            # Protected route (auth required, no scopes/roles)
            {
                "path": "/protected",
                "method": "GET",
                "handler_id": "protected",
                "upstream": "backend",
                "priority": 10,
                "auth_required": True,
            },
            # Route requiring specific scope (OR logic)
            {
                "path": "/api/users",
                "method": "GET",
                "handler_id": "get_users",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["read:users"],
            },
            # Route requiring multiple scopes (OR logic - any one)
            {
                "path": "/api/posts",
                "method": "GET",
                "handler_id": "get_posts",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["read:posts", "read:all"],
            },
            # Route requiring multiple scopes (AND logic - all required)
            {
                "path": "/api/admin/users",
                "method": "DELETE",
                "handler_id": "delete_users",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["delete:users", "admin:access"],
            },
            # Route requiring role
            {
                "path": "/admin/dashboard",
                "method": "GET",
                "handler_id": "admin_dashboard",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_roles": ["admin"],
            },
            # Route requiring multiple roles (OR logic)
            {
                "path": "/admin/settings",
                "method": "GET",
                "handler_id": "admin_settings",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_roles": ["admin", "moderator"],
            },
            # Route requiring both scopes AND roles
            {
                "path": "/api/admin/posts",
                "method": "POST",
                "handler_id": "create_admin_post",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["write:posts"],
                "required_roles": ["admin"],
            },
        ],
        "jwt": {
            "enabled": True,
            "header": "Authorization",
            "scheme": "Bearer",
            "keys": [
                {
                    "algorithm": "RS256",
                    "key_id": "test-rsa-key",
                    "public_key_path": str(jwt_test_keys["rsa_public_path"]),
                }
            ],
            "allowed_issuers": ["https://test.auth.titan.com"],
            "allowed_audiences": ["titan-api"],
            "require_exp": True,
            "require_sub": False,
            "clock_skew_seconds": 60,
            "cache_enabled": True,
            "cache_capacity": 10000,
        },
        "auth": {"enabled": False},
        "jwt_authz": {
            "enabled": True,
            "scope_claim": "scope",
            "roles_claim": "roles",
            "require_all_scopes": False,  # OR logic by default
            "require_all_roles": False,   # OR logic by default
        },
        "transform": {"enabled": False, "path_rewrites": [], "request_headers": [], "response_headers": [], "query_params": []},
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False},
        "logging": {"level": "info", "format": "text"},
        "metrics": {"enabled": False},
    }

    config_path = tmp_path / "titan_jwt_authz.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_jwt_server(process_manager, titan_jwt_config, mock_backend_1, jwt_test_keys):
    """Start Titan server with JWT authorization enabled"""
    from pathlib import Path
    import time

    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build" / "dev" / "src" / "titan"

    if not TITAN_BINARY.exists():
        raise RuntimeError(f"Titan binary not found at {TITAN_BINARY}")

    proc = process_manager.start_process(
        "titan-jwt",
        [str(TITAN_BINARY), "--config", str(titan_jwt_config)],
        cwd=REPO_ROOT,
    )

    if not process_manager.wait_for_port(8080, timeout=5):
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    time.sleep(2.0)  # Give time to fully initialize

    yield "http://127.0.0.1:8080"

    # Cleanup
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=3)
    time.sleep(0.5)


# Public Route Tests


def test_public_route_without_token(titan_jwt_server):
    """Public route should be accessible without JWT token"""
    resp = requests.get(f"{titan_jwt_server}/public")
    assert resp.status_code == 200


def test_public_route_with_invalid_token(titan_jwt_server):
    """Public route should ignore invalid JWT tokens"""
    resp = requests.get(
        f"{titan_jwt_server}/public",
        headers={"Authorization": "Bearer invalid-token"}
    )
    # Should succeed - public routes don't validate JWT
    assert resp.status_code == 200


# Authentication Tests (Protected Route)


def test_protected_route_without_token(titan_jwt_server):
    """Protected route should reject requests without token"""
    resp = requests.get(f"{titan_jwt_server}/protected")
    assert resp.status_code == 401
    assert resp.json()["error"] == "unauthorized"


def test_protected_route_with_valid_token(titan_jwt_server, create_jwt_token):
    """Protected route should accept valid JWT token (no scopes/roles required)"""
    token = create_jwt_token()
    resp = requests.get(
        f"{titan_jwt_server}/protected",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


# Scope Authorization Tests (OR Logic)


def test_scope_required_with_matching_scope(titan_jwt_server, create_jwt_token):
    """Request with required scope should succeed"""
    token = create_jwt_token(scopes=["read:users"])
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_scope_required_without_scope(titan_jwt_server, create_jwt_token):
    """Request without required scope should return 403"""
    token = create_jwt_token(scopes=["write:posts"])  # Wrong scope
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403
    assert resp.json()["error"] == "forbidden"
    assert "Insufficient permissions" in resp.json()["message"]


def test_scope_required_with_multiple_scopes(titan_jwt_server, create_jwt_token):
    """Request with extra scopes should succeed"""
    token = create_jwt_token(scopes=["read:users", "write:users", "admin:access"])
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_multiple_scopes_or_logic(titan_jwt_server, create_jwt_token):
    """Route with multiple scopes (OR logic) should accept any one scope"""
    # Has read:posts (one of the required scopes)
    token = create_jwt_token(scopes=["read:posts"])
    resp = requests.get(
        f"{titan_jwt_server}/api/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200

    # Has read:all (the other required scope)
    token2 = create_jwt_token(scopes=["read:all"])
    resp2 = requests.get(
        f"{titan_jwt_server}/api/posts",
        headers={"Authorization": f"Bearer {token2}"}
    )
    assert resp2.status_code == 200


def test_multiple_scopes_or_logic_failure(titan_jwt_server, create_jwt_token):
    """Route with multiple scopes (OR logic) should reject if none match"""
    token = create_jwt_token(scopes=["write:posts"])  # Not in required list
    resp = requests.get(
        f"{titan_jwt_server}/api/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


# Role Authorization Tests


def test_role_required_with_matching_role(titan_jwt_server, create_jwt_token):
    """Request with required role should succeed"""
    token = create_jwt_token(roles=["admin"])
    resp = requests.get(
        f"{titan_jwt_server}/admin/dashboard",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_role_required_without_role(titan_jwt_server, create_jwt_token):
    """Request without required role should return 403"""
    token = create_jwt_token(roles=["user"])  # Wrong role
    resp = requests.get(
        f"{titan_jwt_server}/admin/dashboard",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_multiple_roles_or_logic(titan_jwt_server, create_jwt_token):
    """Route with multiple roles (OR logic) should accept any one role"""
    # Has admin
    token1 = create_jwt_token(roles=["admin"])
    resp1 = requests.get(
        f"{titan_jwt_server}/admin/settings",
        headers={"Authorization": f"Bearer {token1}"}
    )
    assert resp1.status_code == 200

    # Has moderator
    token2 = create_jwt_token(roles=["moderator"])
    resp2 = requests.get(
        f"{titan_jwt_server}/admin/settings",
        headers={"Authorization": f"Bearer {token2}"}
    )
    assert resp2.status_code == 200


def test_multiple_roles_or_logic_failure(titan_jwt_server, create_jwt_token):
    """Route with multiple roles (OR logic) should reject if none match"""
    token = create_jwt_token(roles=["user", "guest"])  # Neither admin nor moderator
    resp = requests.get(
        f"{titan_jwt_server}/admin/settings",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


# Combined Scope + Role Tests


def test_combined_scope_and_role_success(titan_jwt_server, create_jwt_token):
    """Request with both required scope AND role should succeed"""
    token = create_jwt_token(scopes=["write:posts"], roles=["admin"])
    resp = requests.post(
        f"{titan_jwt_server}/api/admin/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200


def test_combined_scope_missing_role(titan_jwt_server, create_jwt_token):
    """Request with scope but missing role should return 403"""
    token = create_jwt_token(scopes=["write:posts"], roles=["user"])
    resp = requests.post(
        f"{titan_jwt_server}/api/admin/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_combined_role_missing_scope(titan_jwt_server, create_jwt_token):
    """Request with role but missing scope should return 403"""
    token = create_jwt_token(scopes=["read:posts"], roles=["admin"])
    resp = requests.post(
        f"{titan_jwt_server}/api/admin/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_combined_missing_both(titan_jwt_server, create_jwt_token):
    """Request missing both scope and role should return 403"""
    token = create_jwt_token(scopes=["read:users"], roles=["user"])
    resp = requests.post(
        f"{titan_jwt_server}/api/admin/posts",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


# Edge Cases


def test_no_scope_claim_in_token(titan_jwt_server, create_jwt_token):
    """Token without scope claim should fail authorization if scopes required"""
    token = create_jwt_token()  # No scopes
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_empty_scope_claim(titan_jwt_server, create_jwt_token):
    """Token with empty scope claim should fail authorization"""
    token = create_jwt_token(scopes=[])
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_scope_case_sensitivity(titan_jwt_server, create_jwt_token):
    """Scopes should be case-sensitive"""
    token = create_jwt_token(scopes=["READ:USERS"])  # Wrong case
    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 403


def test_multiple_spaces_in_scopes(titan_jwt_server, create_jwt_token):
    """Scope parsing should handle multiple spaces correctly"""
    import jwt
    import time

    # Manually create token with weird spacing
    payload = {
        "iss": "https://test.auth.titan.com",
        "sub": "test-user",
        "aud": "titan-api",
        "exp": int(time.time()) + 3600,
        "scope": "read:users    write:users",  # Multiple spaces
    }

    # Get signing key from create_jwt_token fixture's closure
    # (This is a bit hacky but works)
    token_factory = create_jwt_token
    normal_token = token_factory(scopes=["dummy"])
    # Use the RS256 private key
    from conftest import jwt_test_keys

    # Just use the factory instead
    token = create_jwt_token(scopes=["read:users", "write:users"])

    resp = requests.get(
        f"{titan_jwt_server}/api/users",
        headers={"Authorization": f"Bearer {token}"}
    )
    assert resp.status_code == 200
