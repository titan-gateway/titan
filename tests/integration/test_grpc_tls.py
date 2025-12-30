#!/usr/bin/env python3
"""
gRPC over TLS test - proves gRPC works through Titan with TLS enabled
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
    print("TITAN gRPC OVER TLS TEST")
    print("=" * 60)

    # Step 1: Start gRPC backend
    print("\n[1/5] Starting gRPC backend on port 50051...")
    grpc_server = serve_grpc(port=50051, max_workers=4)
    time.sleep(1)
    print("✓ gRPC backend started")

    # Step 2: Create Titan config with TLS
    print("\n[2/5] Creating Titan TLS configuration...")
    config = {
        "server": {
            "host": "127.0.0.1",
            "port": 8443,  # TLS port
            "worker_threads": 2,
            "read_timeout": 60000,
            "write_timeout": 60000,
            "max_connections": 1000,
            "enable_tls": True,
            "tls_cert_path": "/tmp/cert.pem",
            "tls_key_path": "/tmp/key.pem"
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
                "method": "POST",
                "handler_id": "grpc_greeter",
                "upstream": "grpc_backend",
                "priority": 10
            }
        ],
        "cors": {"enabled": False},
        "rate_limit": {"enabled": False}
    }

    config_path = "/tmp/titan_grpc_tls_config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)
    print(f"✓ TLS config written to {config_path}")

    # Step 3: Start Titan with TLS
    print("\n[3/5] Starting Titan with TLS...")
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

    print("✓ Titan started on port 8443 (TLS)")

    # Step 4: Test gRPC through Titan with TLS
    print("\n[4/5] Testing gRPC over TLS through Titan...")
    try:
        # Create SSL credentials (accept self-signed cert)
        credentials = grpc.ssl_channel_credentials()

        # Connect with TLS, override hostname check for self-signed cert
        channel = grpc.secure_channel(
            'localhost:8443',
            credentials,
            options=[('grpc.ssl_target_name_override', 'localhost')]
        )

        grpc.channel_ready_future(channel).result(timeout=5)

        stub = helloworld_pb2_grpc.GreeterStub(channel)
        response = stub.SayHello(helloworld_pb2.HelloRequest(name="TitanTLS"))

        if response.message == "Hello, TitanTLS!":
            print(f"✓ Received response: {response.message}")
        else:
            print(f"✗ Unexpected response: {response.message}")
            raise ValueError("Unexpected response")

        channel.close()

    except Exception as e:
        import traceback
        print(f"✗ gRPC TLS test failed: {e}")
        print(f"\nFull traceback:")
        traceback.print_exc()
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
    print("✓ SUCCESS - gRPC over TLS works through Titan!")
    print("=" * 60)
    print("\nNow we need to debug why h2c (cleartext) doesn't work...")


if __name__ == '__main__':
    main()
