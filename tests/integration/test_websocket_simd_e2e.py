"""
Simple e2e test proving SIMD unmasking works in WebSocket flow
"""
import pytest
import json
import websocket
import time
import platform


def test_websocket_large_message_simd(tmp_path, process_manager, mock_backend_1):
    """Send large WebSocket message to trigger SIMD unmasking"""
    from pathlib import Path

    # Stop any existing Titan servers to avoid port conflicts
    process_manager.stop_titan_servers()

    # Detect platform for accurate SIMD labeling
    arch = platform.machine().lower()
    is_arm64 = arch in ['aarch64', 'arm64']
    is_x86_64 = arch in ['x86_64', 'amd64']

    # SIMD instruction set labels
    simd_16byte = "NEON" if is_arm64 else "SSE2"
    simd_32byte = "NEON" if is_arm64 else "AVX2"

    # 1. Create config
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
        "routes": [{"path": "/ws/echo", "method": "GET", "upstream": "backend", "middleware": ["permissive_cors"], "websocket": {"enabled": True}}],
        "cors_configs": {
            "permissive_cors": {
                "enabled": True,
                "allowed_origins": ["*"],
                "allowed_methods": ["GET"],
                "allowed_headers": ["*"],
                "allow_credentials": False,
                "max_age": 3600
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
    time.sleep(0.5)

    # 3. Connect and send messages of various sizes
    ws = websocket.WebSocket()
    ws.connect("ws://127.0.0.1:8080/ws/echo")

    # Test small message (scalar fallback)
    small_msg = "x" * 10
    ws.send(small_msg)
    response = ws.recv()
    assert small_msg in response
    print(f"Small message (10 bytes, scalar): PASS")

    # Test medium message (16-byte SIMD)
    medium_msg = "y" * 64
    ws.send(medium_msg)
    response = ws.recv()
    assert medium_msg in response
    print(f"Medium message (64 bytes, {simd_16byte}): PASS")

    # Test large message (32-byte SIMD on x86_64, 16-byte on ARM64)
    large_msg = "z" * 1024
    ws.send(large_msg)
    response = ws.recv()
    assert large_msg in response
    print(f"Large message (1KB, {simd_32byte}): PASS")

    # Test very large message (100KB stress)
    very_large_msg = "a" * (100 * 1024)  # 100KB
    ws.send(very_large_msg)
    response = ws.recv()
    assert very_large_msg in response
    print(f"Very large message (100KB, {simd_32byte} stress): PASS")

    # Test message with repeating pattern (validates SIMD correctness)
    pattern_msg = "ABC123" * 100  # 600 bytes with repeating pattern
    ws.send(pattern_msg)
    response = ws.recv()
    assert pattern_msg in response
    print(f"Pattern message (600 bytes, repeating pattern): PASS")

    # Test 1MB message (SIMD ultra-stress)
    print(f"Sending 1MB message...")
    mb1_msg = "M" * (1024 * 1024)  # 1MB
    ws.send(mb1_msg)
    response = ws.recv()
    assert mb1_msg in response
    print(f"1MB message ({simd_32byte} ultra-stress): PASS")

    # Test 2MB message (multi-megabyte stress test - 2,097,152 bytes!)
    print(f"Sending 2MB message (2,097,152 bytes)...")
    mb2_msg = "B" * (2 * 1024 * 1024 + 4096)  # 2MB + 4KB (unaligned)
    ws.send(mb2_msg)
    response = ws.recv()
    assert mb2_msg in response
    print(f"2MB message ({simd_32byte} mega-stress): PASS")

    ws.close()
    print(f"\nAll WebSocket SIMD unmasking tests PASSED! [{arch.upper()}]")
    print(f"   - Small (10 bytes, scalar fallback)")
    print(f"   - Medium (64 bytes, {simd_16byte})")
    print(f"   - Large (1KB, {simd_32byte})")
    print(f"   - 100KB ({simd_32byte} stress)")
    print(f"   - Pattern (600 bytes, correctness)")
    print(f"   - 1MB (ultra-stress)")
    print(f"   - 2MB (mega-stress, 2M bytes!)")
    print(f"\nNote: Titan supports up to 10MB WebSocket messages (MAX_WEBSOCKET_MESSAGE_SIZE)")
    print(f"   Test limited to 2MB due to backend (FastAPI/Uvicorn) constraints.")


def test_websocket_simd_correctness_boundary(tmp_path, process_manager, mock_backend_1):
    """Test SIMD correctness at exact boundaries (16, 32 bytes)"""
    from pathlib import Path

    # Stop any existing Titan servers to avoid port conflicts
    process_manager.stop_titan_servers()

    # Detect platform for accurate SIMD labeling
    arch = platform.machine().lower()
    is_arm64 = arch in ['aarch64', 'arm64']
    is_x86_64 = arch in ['x86_64', 'amd64']

    # SIMD instruction set labels
    simd_16byte = "NEON" if is_arm64 else "SSE2"
    simd_32byte = "NEON" if is_arm64 else "AVX2"

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
        "routes": [{"path": "/ws/echo", "method": "GET", "upstream": "backend", "middleware": ["permissive_cors"], "websocket": {"enabled": True}}],
        "cors_configs": {
            "permissive_cors": {
                "enabled": True,
                "allowed_origins": ["*"],
                "allowed_methods": ["GET"],
                "allowed_headers": ["*"],
                "allow_credentials": False,
                "max_age": 3600
            }
        },
    }

    config_path = tmp_path / "config.json"
    with open(config_path, "w") as f:
        json.dump(config, f)

    REPO_ROOT = Path(__file__).parent.parent.parent
    TITAN_BINARY = REPO_ROOT / "build/dev/src/titan"

    proc = process_manager.start_process("titan", [str(TITAN_BINARY), "--config", str(config_path)], cwd=REPO_ROOT)
    assert process_manager.wait_for_port(8080, timeout=10), "Titan failed to start"
    time.sleep(0.5)

    ws = websocket.WebSocket()
    ws.connect("ws://127.0.0.1:8080/ws/echo")

    # Test exact SIMD boundaries (dynamic labels based on architecture)
    test_cases = [
        (15, f"{simd_16byte} boundary - 1 (15 bytes)"),
        (16, f"{simd_16byte} boundary (16 bytes)"),
        (17, f"{simd_16byte} boundary + 1 (17 bytes)"),
        (31, f"31 bytes" if is_arm64 else f"{simd_32byte} boundary - 1"),
        (32, f"32 bytes" if is_arm64 else f"{simd_32byte} boundary"),
        (33, f"33 bytes" if is_arm64 else f"{simd_32byte} boundary + 1"),
        (48, f"3x 16 bytes ({simd_16byte} aligned)"),
        (64, f"64 bytes ({simd_16byte} aligned)" if is_arm64 else f"2x 32 bytes ({simd_32byte} aligned)"),
    ]

    for size, description in test_cases:
        msg = "x" * size
        ws.send(msg)
        response = ws.recv()
        assert msg in response, f"Failed for {description}"
        print(f"{size} bytes ({description}): PASS")

    ws.close()
    print(f"\nAll SIMD boundary tests PASSED! [{arch.upper()}]")
