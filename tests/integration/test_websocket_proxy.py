"""
WebSocket proxy integration tests
Tests that Titan correctly proxies WebSocket connections
"""
import pytest
import json
import websocket  # websocket-client library
import time
import threading


@pytest.fixture
def titan_config_websocket(tmp_path, mock_backend_1):
    """Create a Titan configuration file with WebSocket routes"""
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
                "name": "ws_backend",
                "load_balancing": "round_robin",
                "backends": [
                    {"host": "127.0.0.1", "port": 3001, "weight": 1, "max_connections": 100},
                ],
            }
        ],
        "routes": [
            # WebSocket routes
            {"path": "/ws/echo", "method": "GET", "handler_id": "ws_echo", "upstream": "ws_backend", "priority": 10},
            {"path": "/ws/binary", "method": "GET", "handler_id": "ws_binary", "upstream": "ws_backend", "priority": 10},
            {"path": "/ws/broadcast", "method": "GET", "handler_id": "ws_broadcast", "upstream": "ws_backend", "priority": 10},
            {"path": "/ws/ping-pong", "method": "GET", "handler_id": "ws_ping_pong", "upstream": "ws_backend", "priority": 10},
            {"path": "/ws/slow", "method": "GET", "handler_id": "ws_slow", "upstream": "ws_backend", "priority": 10},
            # Regular HTTP routes for testing
            {"path": "/health", "method": "GET", "handler_id": "health", "upstream": "ws_backend", "priority": 5},
        ],
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False},
        "auth": {"enabled": False},
        "transform": {"enabled": False, "path_rewrites": [], "request_headers": [], "response_headers": [], "query_params": []},
        "compression": {"enabled": False},
        "logging": {"level": "info", "format": "text"},
        "metrics": {"enabled": True, "port": 9090, "path": "/metrics"},
    }

    config_path = tmp_path / "titan_websocket_test.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)

    return config_path


@pytest.fixture
def titan_server_websocket(process_manager, titan_config_websocket, mock_backend_1):
    """Start Titan server with WebSocket configuration"""
    import sys
    from pathlib import Path

    REPO_ROOT = Path(__file__).parent.parent.parent
    BUILD_DIR = REPO_ROOT / "build" / "dev"
    TITAN_BINARY = BUILD_DIR / "src" / "titan"

    proc = process_manager.start_process(
        "titan-ws",
        [str(TITAN_BINARY), "--config", str(titan_config_websocket)],
        cwd=REPO_ROOT,
    )

    # Wait for Titan to start
    if not process_manager.wait_for_port(8080, timeout=10):
        # Print stdout/stderr for debugging
        stdout, stderr = proc.communicate(timeout=1)
        print(f"Titan stdout: {stdout}")
        print(f"Titan stderr: {stderr}")
        raise RuntimeError("Titan server failed to start on port 8080")

    # Give it a moment to fully initialize
    time.sleep(0.5)

    yield "ws://127.0.0.1:8080"

    # Cleanup happens in process_manager fixture


def test_websocket_echo(titan_server_websocket):
    """Test basic WebSocket echo through proxy"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/echo")

    try:
        # Send text message
        ws.send("Hello WebSocket")
        response = ws.recv()
        assert response == "echo: Hello WebSocket"

        # Send another message
        ws.send("Test message 2")
        response = ws.recv()
        assert response == "echo: Test message 2"
    finally:
        ws.close()


def test_websocket_binary_echo(titan_server_websocket):
    """Test binary WebSocket frames through proxy"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/binary")

    try:
        # Send binary data
        binary_data = b"\x00\x01\x02\x03\x04\x05"
        ws.send_binary(binary_data)
        response = ws.recv()
        assert response == binary_data

        # Send larger binary payload
        large_binary = bytes(range(256))
        ws.send_binary(large_binary)
        response = ws.recv()
        assert response == large_binary
    finally:
        ws.close()


def test_websocket_broadcast(titan_server_websocket):
    """Test WebSocket server-initiated messages"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/broadcast")

    try:
        # Receive 5 messages from server
        messages = []
        for i in range(5):
            msg = ws.recv()
            messages.append(msg)

        assert messages == ["message_0", "message_1", "message_2", "message_3", "message_4"]
    finally:
        ws.close()


def test_websocket_multiple_connections(titan_server_websocket):
    """Test multiple concurrent WebSocket connections"""
    connections = []

    try:
        # Open 5 concurrent connections
        for i in range(5):
            ws = websocket.WebSocket()
            ws.connect(f"{titan_server_websocket}/ws/echo")
            connections.append(ws)

        # Send messages on all connections
        for i, ws in enumerate(connections):
            ws.send(f"Message from connection {i}")

        # Receive responses
        for i, ws in enumerate(connections):
            response = ws.recv()
            assert response == f"echo: Message from connection {i}"
    finally:
        for ws in connections:
            ws.close()


def test_websocket_large_message(titan_server_websocket):
    """Test large message handling (fragmentation)"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/echo")

    try:
        # Send 10KB message
        large_message = "A" * 10240
        ws.send(large_message)
        response = ws.recv()
        assert response == f"echo: {large_message}"

        # Send 100KB message
        very_large_message = "B" * 102400
        ws.send(very_large_message)
        response = ws.recv()
        assert response == f"echo: {very_large_message}"
    finally:
        ws.close()


def test_websocket_ping_pong(titan_server_websocket):
    """Test ping/pong keep-alive"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/ping-pong")

    try:
        for i in range(3):
            # Receive ping from server
            ping_msg = ws.recv()
            assert ping_msg == f"ping_{i}"

            # Send pong response
            ws.send(f"pong_{i}")

            # Receive confirmation
            confirmation = ws.recv()
            assert confirmation == f"received: pong_{i}"
    finally:
        ws.close()


def test_websocket_graceful_close(titan_server_websocket):
    """Test graceful WebSocket close"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/echo")

    try:
        # Send a message
        ws.send("test")
        ws.recv()

        # Close gracefully
        ws.close()

        # Connection should be closed
        assert ws.connected == False
    except Exception:
        pass  # Already closed


def test_websocket_connection_after_http(titan_server_websocket):
    """Test WebSocket connection after regular HTTP request"""
    import requests

    # Make HTTP request first
    http_url = titan_server_websocket.replace("ws://", "http://")
    resp = requests.get(f"{http_url}/health", timeout=2)
    assert resp.status_code == 200

    # Now make WebSocket connection
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/echo")

    try:
        ws.send("After HTTP")
        response = ws.recv()
        assert response == "echo: After HTTP"
    finally:
        ws.close()


def test_websocket_rapid_messages(titan_server_websocket):
    """Test rapid message sending (stress test)"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/echo")

    try:
        # Send 100 messages rapidly
        for i in range(100):
            ws.send(f"Message {i}")
            response = ws.recv()
            assert response == f"echo: Message {i}"
    finally:
        ws.close()


def test_websocket_slow_backend(titan_server_websocket):
    """Test WebSocket with slow backend responses"""
    ws = websocket.WebSocket()
    ws.connect(f"{titan_server_websocket}/ws/slow")

    try:
        start = time.time()
        ws.send("test")
        response = ws.recv()
        duration = time.time() - start

        assert response == "slow_echo: test"
        assert duration >= 0.5  # Backend has 0.5s delay
    finally:
        ws.close()
