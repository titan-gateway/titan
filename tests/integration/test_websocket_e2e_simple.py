"""
Super simple WebSocket middleware e2e test
Just proves CORS blocking works end-to-end
"""
import pytest
import json
import websocket


def test_websocket_cors_blocks_bad_origin(tmp_path, process_manager, mock_backend_1):
    """Dead simple test: CORS should block invalid origin"""
    from pathlib import Path

    # Stop any existing Titan servers to avoid port conflicts
    process_manager.stop_titan_servers()

    # 1. Create config with strict CORS
    config = {
        "server": {
            "worker_threads": 1,
            "listen_address": "127.0.0.1",
            "listen_port": 8080,
            "websocket": {
                "enabled": True,
                "max_frame_size": 1048576,
                "max_message_size": 10485760,
                "idle_timeout": 300,
                "ping_interval": 30,
                "max_connections_per_worker": 10000
            }
        },
        "upstreams": [{"name": "backend", "backends": [{"host": "127.0.0.1", "port": 3001}]}],
        "routes": [{"path": "/ws/echo", "method": "GET", "upstream": "backend", "middleware": ["strict_cors"], "websocket": {"enabled": True}}],
        "cors_configs": {
            "strict_cors": {
                "enabled": True,
                "allowed_origins": ["https://good.com"],  # Only this origin allowed
                "allowed_methods": ["GET"],
                "allowed_headers": ["*"],
            }
        },
    }

    config_path = tmp_path / "config.json"
    with open(config_path, "w") as f:
        json.dump(config, f)

    # 2. Start Titan
    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build/dev/src/titan"

    proc = process_manager.start_process("titan", [str(TITAN_BINARY), "--config", str(config_path)], cwd=REPO_ROOT)
    assert process_manager.wait_for_port(8080, timeout=10), "Titan failed to start"

    # 3. Try to connect with bad origin - should fail
    ws = websocket.WebSocket()
    try:
        ws.connect("ws://127.0.0.1:8080/ws/echo", origin="https://evil.com")
        assert False, "Should have been blocked by CORS!"
    except websocket.WebSocketBadStatusException as e:
        assert e.status_code == 403  # Forbidden
        assert "Origin not allowed" in str(e)

    # 4. Connect with good origin - should work
    ws2 = websocket.WebSocket()
    ws2.connect("ws://127.0.0.1:8080/ws/echo", origin="https://good.com")
    ws2.send("hello")
    response = ws2.recv()
    assert "hello" in response
    ws2.close()
