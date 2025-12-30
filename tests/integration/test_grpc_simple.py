#!/usr/bin/env python3
"""
Simple gRPC smoke test to validate basic functionality before running full suite
"""

import json
import subprocess
import sys
import time
from pathlib import Path

import grpc
from backends.grpc_server import serve as serve_grpc
from protos import helloworld_pb2, helloworld_pb2_grpc


def main():
    print("=" * 60)
    print("TITAN gRPC SMOKE TEST")
    print("=" * 60)

    # Step 1: Start gRPC backend
    print("\n[1/5] Starting gRPC backend on port 50051...")
    grpc_server = serve_grpc(port=50051, max_workers=4)
    time.sleep(1)
    print("✓ gRPC backend started")

    # Step 2: Create Titan config
    print("\n[2/5] Creating Titan configuration...")
    config = {
        "server": {
            "host": "127.0.0.1",
            "port": 8080,
            "worker_threads": 2,
            "read_timeout": 60000,
            "write_timeout": 60000,
            "max_connections": 1000,
            "enable_tls": False
        },
        "upstreams": [
            {
                "name": "grpc_backend",
                "backends": [
                    {"host": "127.0.0.1", "port": 50051, "weight": 1, "max_connections": 100}
                ]
            }
        ],
        "routes": [
            {
                "path": "/helloworld.Greeter/*",
                "method": "POST",  # gRPC always uses POST
                "handler_id": "grpc_greeter",
                "upstream": "grpc_backend",
                "priority": 10
            }
        ],
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False}
    }

    config_path = "/tmp/titan_grpc_test_config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)
    print(f"✓ Config written to {config_path}")

    # Step 3: Start Titan
    print("\n[3/5] Starting Titan proxy...")
    titan_path = Path("/workspace/build/dev/src/titan")
    if not titan_path.exists():
        print(f"✗ Titan binary not found at {titan_path}")
        grpc_server.stop(0)
        sys.exit(1)

    titan_process = subprocess.Popen(
        [str(titan_path), "--config", config_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Wait for Titan to start
    time.sleep(3)

    if titan_process.poll() is not None:
        stdout, stderr = titan_process.communicate()
        print(f"✗ Titan failed to start")
        print(f"STDOUT: {stdout}")
        print(f"STDERR: {stderr}")
        grpc_server.stop(0)
        sys.exit(1)

    print("✓ Titan started on port 8080")

    # Step 4: Test gRPC through Titan
    print("\n[4/5] Testing gRPC unary RPC through Titan...")
    try:
        channel = grpc.insecure_channel('localhost:8080')
        grpc.channel_ready_future(channel).result(timeout=5)

        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name="TitanTest"))

        if response.message == "Hello, TitanTest!":
            print(f"✓ Received response: {response.message}")
        else:
            print(f"✗ Unexpected response: {response.message}")
            raise ValueError("Unexpected response")

        channel.close()

    except Exception as e:
        print(f"✗ gRPC test failed: {e}")
        titan_process.terminate()
        grpc_server.stop(0)
        sys.exit(1)

    # Step 5: Cleanup
    print("\n[5/5] Cleaning up...")
    titan_process.terminate()
    titan_process.wait(timeout=5)
    grpc_server.stop(0)
    print("✓ Cleanup complete")

    print("\n" + "=" * 60)
    print("✓ ALL TESTS PASSED - gRPC proxy working!")
    print("=" * 60)


if __name__ == '__main__':
    main()
