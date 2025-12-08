"""
Integration tests for JWT Security Hardening

Tests malformed and malicious JWT payloads against running Titan server
to validate security limits and protections (Phases 6-8).
"""
import json
import pytest
import requests
import time


@pytest.fixture
def titan_jwt_security_config(tmp_path, jwt_test_keys, mock_backend_1):
    """Titan config for security testing with JWT + authorization"""
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
            # Public route (no auth)
            {
                "path": "/public",
                "method": "GET",
                "handler_id": "public",
                "upstream": "backend",
                "priority": 10,
                "auth_required": False,
            },
            # Protected route (auth only, no scopes)
            {
                "path": "/protected",
                "method": "GET",
                "handler_id": "protected",
                "upstream": "backend",
                "priority": 10,
                "auth_required": True,
            },
            # Scope-gated routes (using standard API paths like test_jwt_authz.py)
            {
                "path": "/api/users",
                "method": "GET",
                "handler_id": "get_users",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["scope99"],  # Test specific scope
            },
            {
                "path": "/api/posts",
                "method": "GET",
                "handler_id": "get_posts",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["scope149"],  # Test truncation
            },
            {
                "path": "/admin/dashboard",
                "method": "GET",
                "handler_id": "admin_dashboard",
                "upstream": "backend",
                "priority": 5,
                "auth_required": True,
                "required_scopes": ["admin"],
            },
        ],
        "jwt": {
            "enabled": True,
            "header": "Authorization",
            "scheme": "Bearer",
            "keys": [
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
        "auth": {"enabled": False},
        "jwt_authz": {
            "enabled": True,
            "require_all_scopes": False,  # OR logic for testing
            "require_all_roles": False,
        },
        "transform": {"enabled": False, "path_rewrites": [], "request_headers": [], "response_headers": [], "query_params": []},
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False},
        "logging": {"level": "warning", "format": "text"},
        "metrics": {"enabled": False},
    }

    config_path = tmp_path / "titan_jwt_security.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_jwt_security_server(process_manager, titan_jwt_security_config, mock_backend_1, jwt_test_keys):
    """Start Titan server for security testing"""
    from pathlib import Path

    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build" / "dev" / "src" / "titan"

    if not TITAN_BINARY.exists():
        raise RuntimeError(f"Titan binary not found at {TITAN_BINARY}")

    proc = process_manager.start_process(
        "titan-jwt-security",
        [str(TITAN_BINARY), "--config", str(titan_jwt_security_config)],
        cwd=REPO_ROOT,
    )

    if not process_manager.wait_for_port(8080, timeout=5):
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan failed to start on port 8080")

    time.sleep(1.0)

    yield "http://127.0.0.1:8080"

    # Cleanup
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.communicate(timeout=3)
        except:
            proc.kill()


class TestJWTSecurityHardening:
    """Integration tests for JWT security limits (Phases 6-8)"""

    def test_excessive_scope_count_truncated(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Security Test: Excessive scope count (> 100)
        Expected: Token accepted but scopes truncated to first 100

        This validates MAX_SCOPE_ROLE_COUNT limit (src/core/jwt.hpp:41)
        """
        # Create JWT with 150 scopes (exceeds MAX_SCOPE_ROLE_COUNT=100)
        scopes_list = [f"scope{i}" for i in range(150)]

        token = create_jwt_token(algorithm="HS256", scopes=scopes_list)

        # Request route requiring scope99 (within first 100) - should succeed
        resp = requests.get(
            f"{titan_jwt_security_server}/api/users",
            headers={"Authorization": f"Bearer {token}"}
        )
        assert resp.status_code == 200, f"Expected 200 for scope99, got {resp.status_code}"

        # Request route requiring scope149 (beyond 100) - should fail with 403
        resp = requests.get(
            f"{titan_jwt_security_server}/api/posts",
            headers={"Authorization": f"Bearer {token}"}
        )
        assert resp.status_code == 403, f"Expected 403 for scope149 (truncated), got {resp.status_code}"


    def test_unicode_and_special_chars_handled(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Security Test: Unicode and special characters in scopes
        Expected: Handled gracefully without crashes (no 500 errors)

        Tests sanitize_for_logging() implementation
        """
        test_cases = [
            "read:users 😈 admin",  # Emoji
            "роль_администратора",  # Cyrillic
            "scope\tadmin",  # Tab character
            "scope  admin",  # Multiple spaces
            "a" * 1000,  # Very long scope name
            "",  # Empty scope
            "   ",  # Only whitespace
        ]

        for malicious_scope in test_cases:
            # Pass custom scope string via extra_claims
            token = create_jwt_token(algorithm="HS256", scope=malicious_scope)

            resp = requests.get(
                f"{titan_jwt_security_server}/protected",
                headers={"Authorization": f"Bearer {token}"}
            )

            # Server should handle gracefully (200 or 401/403, but NEVER 500)
            assert resp.status_code in [200, 401, 403], \
                f"Server crashed with {resp.status_code} for scope: {repr(malicious_scope)}"


    def test_log_injection_attempt(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Security Test: Log injection with control characters
        Expected: Request fails authorization, server doesn't crash

        Full validation would require log file inspection, but we verify:
        1. Server doesn't crash (no 500)
        2. Authorization correctly fails (403)
        3. Control characters in scope don't break request processing
        """
        # Attempt log injection with newlines and control chars
        malicious_scope = "read:users\n[ERROR] FAKE LOG ENTRY\nadmin"

        token = create_jwt_token(algorithm="HS256", scope=malicious_scope, sub="attacker")

        # Try to access admin route (should be rejected - invalid scope claim)
        # SECURITY: Tokens with control characters in scope should be rejected (401)
        # NOT allowed to escalate privileges via newline injection
        resp = requests.get(
            f"{titan_jwt_security_server}/admin/dashboard",
            headers={"Authorization": f"Bearer {token}"}
        )

        # MUST reject token with malformed scope claim (401 Unauthorized)
        # Returning 200 would be a privilege escalation vulnerability!
        assert resp.status_code == 401, \
            f"SECURITY FAILURE: Token with control chars in scope must be rejected, got {resp.status_code}"


    def test_hash_based_authorization_performance(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Performance Test: Hash-based authorization (Phase 6)
        Expected: Fast authorization with many scopes (O(n+m) not O(n×m))

        Validates parse_space_separated_set() implementation
        """
        # Create JWT with 100 scopes
        scopes_list = [f"scope{i}" for i in range(100)]

        token = create_jwt_token(algorithm="HS256", scopes=scopes_list)

        # Measure response time for scope check
        start = time.time()
        resp = requests.get(
            f"{titan_jwt_security_server}/api/users",
            headers={"Authorization": f"Bearer {token}"}
        )
        duration_ms = (time.time() - start) * 1000

        assert resp.status_code == 200, f"Expected 200, got {resp.status_code}"

        # With O(n+m) hash-based lookup, this should be very fast (< 100ms even in debug build)
        # Note: This is a smoke test, not a precise benchmark
        assert duration_ms < 100, f"Authorization too slow: {duration_ms:.2f}ms"


    def test_cache_with_varying_token_sizes(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Cache Test: Token cache with varying claim sizes (Phase 7)
        Expected: Server handles large tokens without memory exhaustion

        Validates ThreadLocalTokenCache size-based eviction
        """
        # Send tokens with varying scope sizes
        for i in range(20):
            # Vary scope size from small to large
            scope_size = 100 * (i + 1)  # 100 bytes to 2KB
            large_scope = "read:users " + ("x" * scope_size)

            token = create_jwt_token(algorithm="HS256", scope=large_scope, sub=f"user{i}")

            resp = requests.get(
                f"{titan_jwt_security_server}/protected",
                headers={"Authorization": f"Bearer {token}"}
            )

            # Server should handle all tokens without crashing
            assert resp.status_code == 200, \
                f"Failed with token {i} (scope size {scope_size}): {resp.status_code}"


    def test_empty_and_whitespace_scopes(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Edge Case Test: Empty and whitespace-only scopes
        Expected: Valid whitespace (spaces) accepted, control characters rejected
        """
        # Valid cases: empty or space-only scopes (OAuth 2.0 uses space as delimiter)
        valid_cases = [
            "",  # Empty scope
            "   ",  # Only spaces
            "  read:users  ",  # Leading/trailing whitespace
        ]

        for scope in valid_cases:
            token = create_jwt_token(algorithm="HS256", scope=scope)

            resp = requests.get(
                f"{titan_jwt_security_server}/protected",
                headers={"Authorization": f"Bearer {token}"}
            )

            # Should succeed (protected route doesn't require scopes)
            assert resp.status_code == 200, \
                f"Failed with valid scope {repr(scope)}: {resp.status_code}"

        # Invalid cases: control characters (security: prevent injection attacks)
        invalid_cases = [
            "\t",  # Tab
            "\n",  # Newline
            "\r",  # Carriage return
            "\t\n\r",  # Mixed control characters
        ]

        for scope in invalid_cases:
            token = create_jwt_token(algorithm="HS256", scope=scope)

            resp = requests.get(
                f"{titan_jwt_security_server}/protected",
                headers={"Authorization": f"Bearer {token}"}
            )

            # MUST reject tokens with control characters (401 Unauthorized)
            assert resp.status_code == 401, \
                f"SECURITY: Token with control char {repr(scope)} must be rejected, got {resp.status_code}"


    def test_scope_parsing_correctness(self, titan_jwt_security_server, jwt_test_keys, create_jwt_token):
        """
        Correctness Test: Verify scope parsing matches expected behavior
        Expected: Space-separated scopes parsed correctly
        """
        # Token with specific scopes
        token = create_jwt_token(algorithm="HS256", scopes=["read:users", "write:posts", "scope99"])

        # Should succeed - has scope99
        resp = requests.get(
            f"{titan_jwt_security_server}/api/users",
            headers={"Authorization": f"Bearer {token}"}
        )
        assert resp.status_code == 200

        # Should fail - doesn't have admin
        resp = requests.get(
            f"{titan_jwt_security_server}/admin/dashboard",
            headers={"Authorization": f"Bearer {token}"}
        )
        assert resp.status_code == 403
