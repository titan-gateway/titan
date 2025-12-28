#!/usr/bin/env python3
"""
gRPC test server for Titan integration tests

This server implements all four gRPC streaming patterns:
- Unary RPC
- Server streaming
- Client streaming
- Bidirectional streaming
"""

import sys
import time
from concurrent import futures

import grpc
from protos import helloworld_pb2, helloworld_pb2_grpc


class GreeterServicer(helloworld_pb2_grpc.GreeterServicer):
    """Implementation of the Greeter service"""

    def SayHello(self, request, context):
        """Unary RPC: Single request -> single response"""
        print(f"[gRPC Server] Unary RPC: Received request from {request.name}")

        # Set grpc-status in trailers (0 = OK)
        # Note: grpcio automatically sets this, but we can customize
        return helloworld_pb2.HelloReply(message=f"Hello, {request.name}!")

    def SayHelloStream(self, request, context):
        """Server streaming RPC: Single request -> stream of responses"""
        print(f"[gRPC Server] Server Streaming: Received request from {request.name}")

        # Stream 5 greetings
        for i in range(5):
            yield helloworld_pb2.HelloReply(
                message=f"Hello #{i+1}, {request.name}!"
            )
            time.sleep(0.1)  # Small delay to simulate streaming

    def SayHelloClientStream(self, request_iterator, context):
        """Client streaming RPC: Stream of requests -> single response"""
        print("[gRPC Server] Client Streaming: Waiting for requests...")

        names = []
        for request in request_iterator:
            print(f"[gRPC Server] Client Streaming: Received {request.name}")
            names.append(request.name)

        return helloworld_pb2.HelloReply(
            message=f"Hello to all: {', '.join(names)}!"
        )

    def SayHelloBidirectional(self, request_iterator, context):
        """Bidirectional streaming RPC: Stream <-> Stream"""
        print("[gRPC Server] Bidirectional Streaming: Started")

        for request in request_iterator:
            print(f"[gRPC Server] Bidirectional: Received {request.name}")
            yield helloworld_pb2.HelloReply(
                message=f"Echo: Hello, {request.name}!"
            )


def serve(port=50051, max_workers=10):
    """Start the gRPC server"""
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=max_workers))
    helloworld_pb2_grpc.add_GreeterServicer_to_server(GreeterServicer(), server)
    server.add_insecure_port(f'[::]:{port}')
    server.start()
    print(f"[gRPC Server] Started on port {port}")
    return server


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 50051
    server = serve(port)

    try:
        # Keep server running
        while True:
            time.sleep(86400)  # Sleep for 1 day
    except KeyboardInterrupt:
        print("\n[gRPC Server] Shutting down...")
        server.stop(0)
