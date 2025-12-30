"""
Titan gRPC Integration Tests

Tests gRPC passthrough proxying through Titan API Gateway.
Covers all four streaming patterns:
- Unary RPC
- Server streaming
- Client streaming
- Bidirectional streaming
"""

import json
import subprocess
import time
from pathlib import Path

import grpc
import pytest
from backends.grpc_server import serve as serve_grpc
from protos import helloworld_pb2, helloworld_pb2_grpc


@pytest.fixture(scope="module")
def grpc_backend():
    """Start gRPC backend server on port 50051"""
    print("\n[Test] Starting gRPC backend server...")
    server = serve_grpc(port=50051, max_workers=10)
    time.sleep(0.5)  # Wait for server to start
    yield server
    print("\n[Test] Stopping gRPC backend server...")
    server.stop(0)


@pytest.fixture(scope="module")
def titan_config(tmp_path_factory):
    """Create Titan config for gRPC proxying"""
    config_dir = tmp_path_factory.mktemp("config")
    config_file = config_dir / "grpc_config.json"

    config = {
        "server": {
            "host": "127.0.0.1",
            "port": 8080,
            "worker_threads": 2,
            "max_connections": 1000,
            "tls": {
                "enabled": False
            }
        },
        "routes": [
            {
                "path": "/helloworld.Greeter/*",
                "upstream": "grpc_backend",
                "methods": ["POST"],  # gRPC always uses POST
            }
        ],
        "upstreams": {
            "grpc_backend": {
                "servers": [
                    {
                        "host": "127.0.0.1",
                        "port": 50051,
                        "protocol": "http2",  # gRPC requires HTTP/2
                        "weight": 1
                    }
                ],
                "health_check": {
                    "enabled": False
                }
            }
        }
    }

    config_file.write_text(json.dumps(config, indent=2))
    return str(config_file)


@pytest.fixture(scope="module")
def titan_server(titan_config, grpc_backend):
    """Start Titan server with gRPC config"""
    print("\n[Test] Starting Titan server...")

    titan_path = Path("/workspace/build/dev/src/titan")
    if not titan_path.exists():
        pytest.skip("Titan binary not found")

    process = subprocess.Popen(
        [str(titan_path), "--config", titan_config],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Wait for Titan to start
    time.sleep(2)

    if process.poll() is not None:
        stdout, stderr = process.communicate()
        pytest.fail(f"Titan failed to start:\nSTDOUT: {stdout}\nSTDERR: {stderr}")

    yield process

    print("\n[Test] Stopping Titan server...")
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()


@pytest.fixture
def grpc_channel(titan_server):
    """Create gRPC channel to Titan (port 8080)"""
    # Connect to Titan, which will proxy to backend
    channel = grpc.insecure_channel('localhost:8080')

    # Wait for channel to be ready
    try:
        grpc.channel_ready_future(channel).result(timeout=5)
    except grpc.FutureTimeoutError:
        pytest.fail("Failed to connect to Titan gRPC proxy")

    yield channel
    channel.close()


# ============================
# Unary RPC Tests
# ============================

def test_unary_rpc_basic(grpc_channel):
    """Test basic unary RPC through Titan"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    response = stub.SayHello(helloworld_pb2.HelloRequest(name="Titan"))

    assert response.message == "Hello, Titan!"


def test_unary_rpc_multiple_requests(grpc_channel):
    """Test multiple unary RPCs on same channel"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    names = ["Alice", "Bob", "Charlie"]
    for name in names:
        response = stub.SayHello(helloworld_pb2.HelloRequest(name=name))
        assert response.message == f"Hello, {name}!"


def test_unary_rpc_empty_name(grpc_channel):
    """Test unary RPC with empty name"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    response = stub.SayHello(helloworld_pb2.HelloRequest(name=""))

    assert response.message == "Hello, !"


# ============================
# Server Streaming Tests
# ============================

def test_server_streaming_basic(grpc_channel):
    """Test server streaming RPC through Titan"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    responses = list(stub.SayHelloStream(helloworld_pb2.HelloRequest(name="StreamTest")))

    assert len(responses) == 5
    for i, response in enumerate(responses):
        assert response.message == f"Hello #{i+1}, StreamTest!"


def test_server_streaming_early_termination(grpc_channel):
    """Test client terminating server stream early"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    responses = stub.SayHelloStream(helloworld_pb2.HelloRequest(name="EarlyStop"))

    # Consume only 3 responses, then break
    count = 0
    for response in responses:
        count += 1
        assert "Hello" in response.message
        if count == 3:
            break

    assert count == 3


# ============================
# Client Streaming Tests
# ============================

def test_client_streaming_basic(grpc_channel):
    """Test client streaming RPC through Titan"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def request_generator():
        names = ["Alice", "Bob", "Charlie"]
        for name in names:
            yield helloworld_pb2.HelloRequest(name=name)

    response = stub.SayHelloClientStream(request_generator())

    assert response.message == "Hello to all: Alice, Bob, Charlie!"


def test_client_streaming_single_request(grpc_channel):
    """Test client streaming with single request"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def request_generator():
        yield helloworld_pb2.HelloRequest(name="Solo")

    response = stub.SayHelloClientStream(request_generator())

    assert response.message == "Hello to all: Solo!"


# ============================
# Bidirectional Streaming Tests
# ============================

def test_bidirectional_streaming_basic(grpc_channel):
    """Test bidirectional streaming RPC through Titan"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def request_generator():
        names = ["Alice", "Bob", "Charlie"]
        for name in names:
            yield helloworld_pb2.HelloRequest(name=name)

    responses = list(stub.SayHelloBidirectional(request_generator()))

    assert len(responses) == 3
    assert responses[0].message == "Echo: Hello, Alice!"
    assert responses[1].message == "Echo: Hello, Bob!"
    assert responses[2].message == "Echo: Hello, Charlie!"


def test_bidirectional_streaming_interleaved(grpc_channel):
    """Test bidirectional streaming with interleaved send/recv"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def request_generator():
        for i in range(10):
            yield helloworld_pb2.HelloRequest(name=f"User{i}")
            time.sleep(0.01)  # Small delay

    responses = list(stub.SayHelloBidirectional(request_generator()))

    assert len(responses) == 10
    for i, response in enumerate(responses):
        assert response.message == f"Echo: Hello, User{i}!"


# ============================
# Error Handling Tests
# ============================

def test_invalid_service_path(grpc_channel):
    """Test request to non-existent service"""
    channel = grpc.insecure_channel('localhost:8080')

    # Try to call a non-existent service
    stub = grpc.unary_unary(
        '/invalid.Service/Method',
        request_serializer=helloworld_pb2.HelloRequest.SerializeToString,
        response_deserializer=helloworld_pb2.HelloReply.FromString,
    )(channel)

    with pytest.raises(grpc.RpcError) as exc_info:
        stub(helloworld_pb2.HelloRequest(name="Test"))

    # Should get NOT_FOUND or UNIMPLEMENTED
    assert exc_info.value.code() in [grpc.StatusCode.NOT_FOUND, grpc.StatusCode.UNIMPLEMENTED]


# ============================
# Concurrent Request Tests
# ============================

def test_concurrent_unary_requests(grpc_channel):
    """Test multiple concurrent unary RPCs"""
    import concurrent.futures

    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def make_request(name):
        response = stub.SayHello(helloworld_pb2.HelloRequest(name=name))
        return response.message

    with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
        futures = [executor.submit(make_request, f"User{i}") for i in range(50)]
        results = [f.result() for f in futures]

    assert len(results) == 50
    for i, result in enumerate(results):
        assert result == f"Hello, User{i}!"


def test_concurrent_streaming_requests(grpc_channel):
    """Test multiple concurrent streaming RPCs"""
    import concurrent.futures

    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    def make_streaming_request(name):
        responses = list(stub.SayHelloStream(helloworld_pb2.HelloRequest(name=name)))
        return len(responses)

    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
        futures = [executor.submit(make_streaming_request, f"Stream{i}") for i in range(10)]
        results = [f.result() for f in futures]

    assert all(count == 5 for count in results)


# ============================
# Performance Tests
# ============================

def test_throughput_unary_rpc(grpc_channel):
    """Measure throughput for unary RPCs"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    num_requests = 1000
    start_time = time.time()

    for i in range(num_requests):
        stub.SayHello(helloworld_pb2.HelloRequest(name=f"Perf{i}"))

    elapsed = time.time() - start_time
    throughput = num_requests / elapsed

    print(f"\n[Performance] Unary RPC throughput: {throughput:.2f} req/s")
    assert throughput > 100  # Should handle at least 100 req/s


# ============================
# Metadata/Headers Tests
# ============================

def test_grpc_metadata_passthrough(grpc_channel):
    """Test that gRPC metadata (headers) are passed through"""
    stub = helloworld_pb2_grpc.GreeterStub(grpc_channel)

    # Send custom metadata
    metadata = [
        ('x-custom-header', 'test-value'),
        ('authorization', 'Bearer token123')
    ]

    response = stub.SayHello(
        helloworld_pb2.HelloRequest(name="MetadataTest"),
        metadata=metadata
    )

    assert response.message == "Hello, MetadataTest!"
