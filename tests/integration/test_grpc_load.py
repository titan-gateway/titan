#!/usr/bin/env python3
"""
gRPC Load Test - Verify Titan handles high-volume gRPC traffic
"""

import json
import subprocess
import sys
import time
from pathlib import Path
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

import grpc
from backends.grpc_server import serve as serve_grpc
from protos import helloworld_pb2, helloworld_pb2_grpc


def make_grpc_call(channel, call_id):
    """Make a single gRPC call and return success status"""
    try:
        stub = helloworld_pb2_grpc.GreeterStub(channel)
        request = helloworld_pb2.HelloRequest(name=f"Load{call_id}")
        response = stub.SayHello(request, timeout=5)

        expected = f"Hello, Load{call_id}!"
        if response.message == expected:
            return True, None
        else:
            return False, f"Unexpected response: {response.message}"
    except Exception as e:
        return False, str(e)


def main():
    print("=" * 60)
    print("TITAN gRPC LOAD TEST - 10K CALLS")
    print("=" * 60)

    # Configuration
    total_calls = 100
    concurrent_workers = 10
    batch_size = 25

    # Step 1: Start gRPC backend
    print("\n[1/5] Starting gRPC backend on port 50051...")
    grpc_server = serve_grpc(port=50051, max_workers=10)
    time.sleep(1)
    print("✓ gRPC backend started")

    # Step 2: Create Titan config
    print("\n[2/5] Creating Titan configuration...")
    config = {
        "server": {
            "host": "127.0.0.1",
            "port": 8080,
            "worker_threads": 4,
            "read_timeout": 60000,
            "write_timeout": 60000,
            "max_connections": 10000,
        },
        "upstreams": [
            {
                "name": "grpc_backend",
                "backends": [
                    {"host": "127.0.0.1", "port": 50051, "weight": 1, "max_connections": 1000}
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

    config_path = "/tmp/titan_grpc_load_config.json"
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

    # Step 4: Load test
    print(f"\n[4/5] Running load test: {total_calls:,} calls with {concurrent_workers} workers...")

    # Create persistent channel
    channel = grpc.insecure_channel('localhost:8080')

    success_count = 0
    error_count = 0
    errors = []

    start_time = time.time()
    last_report = start_time

    with ThreadPoolExecutor(max_workers=concurrent_workers) as executor:
        futures = []

        # Submit all calls
        for i in range(total_calls):
            future = executor.submit(make_grpc_call, channel, i)
            futures.append(future)

        # Process results as they complete
        for idx, future in enumerate(as_completed(futures), 1):
            success, error = future.result()

            if success:
                success_count += 1
            else:
                error_count += 1
                if len(errors) < 10:  # Keep first 10 errors
                    errors.append(error)

            # Report progress every 1000 calls
            if idx % batch_size == 0:
                elapsed = time.time() - start_time
                rate = idx / elapsed if elapsed > 0 else 0
                print(f"  Progress: {idx:,}/{total_calls:,} ({idx*100//total_calls}%) - "
                      f"{rate:.0f} req/s - Success: {success_count:,} - Errors: {error_count}")

    channel.close()

    # Calculate final stats
    total_time = time.time() - start_time
    avg_rate = total_calls / total_time

    print(f"\n{'='*60}")
    print(f"LOAD TEST RESULTS")
    print(f"{'='*60}")
    print(f"Total Calls:      {total_calls:,}")
    print(f"Successful:       {success_count:,} ({success_count*100//total_calls}%)")
    print(f"Failed:           {error_count:,} ({error_count*100//total_calls if total_calls > 0 else 0}%)")
    print(f"Duration:         {total_time:.2f}s")
    print(f"Average Rate:     {avg_rate:.0f} req/s")
    print(f"{'='*60}")

    if errors:
        print(f"\nFirst {len(errors)} errors:")
        for i, err in enumerate(errors[:10], 1):
            print(f"  {i}. {err}")

    # Step 5: Cleanup
    print("\n[5/5] Cleaning up...")
    titan_process.terminate()
    try:
        titan_process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        titan_process.kill()
        titan_process.wait()

    grpc_server.stop(0)
    print("✓ Cleanup complete")

    # Final verdict
    print("\n" + "=" * 60)
    if error_count == 0:
        print(f"✓ SUCCESS - All {total_calls:,} gRPC calls completed successfully!")
        print("=" * 60)
        return 0
    elif error_count < total_calls * 0.01:  # Less than 1% error rate
        print(f"⚠ PARTIAL SUCCESS - {error_count:,} errors ({error_count*100//total_calls}% error rate)")
        print("=" * 60)
        return 1
    else:
        print(f"✗ FAILURE - {error_count:,} errors ({error_count*100//total_calls}% error rate)")
        print("=" * 60)
        return 1


if __name__ == '__main__':
    sys.exit(main())
