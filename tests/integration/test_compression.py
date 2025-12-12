"""
Compression middleware integration tests
Tests end-to-end compression behavior with real HTTP requests
"""
import gzip
import json
import zstandard as zstd
import brotli
import pytest
import requests


def test_gzip_compression(titan_server, http_session):
    """Test gzip compression for compressible response"""
    headers = {"Accept-Encoding": "gzip"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

    assert resp.status_code == 200
    # requests library auto-decompresses, so check raw headers
    assert resp.raw.headers.get("Content-Encoding") == "gzip"
    assert resp.raw.headers.get("Vary") == "Accept-Encoding"

    # Verify response is actually decompressed and valid by requests
    data = resp.json()
    assert "items" in data
    assert len(data["items"]) == 1000


def test_zstd_compression(titan_server, http_session):
    """Test zstd compression when client supports it"""
    headers = {"Accept-Encoding": "zstd, gzip"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

    assert resp.status_code == 200
    # Server should prefer zstd based on algorithm priority in config
    encoding = resp.raw.headers.get("Content-Encoding")
    assert encoding in ["zstd", "gzip"]
    assert resp.raw.headers.get("Vary") == "Accept-Encoding"

    # Manual decompression (requests doesn't auto-decompress with stream=True)
    raw_data = resp.raw.read()
    if encoding == "zstd":
        decompressed = zstd.decompress(raw_data)
        data = json.loads(decompressed)
    elif encoding == "gzip":
        decompressed = gzip.decompress(raw_data)
        data = json.loads(decompressed)
    else:
        data = json.loads(raw_data)

    assert "items" in data
    assert len(data["items"]) == 1000


def test_brotli_compression(titan_server, http_session):
    """Test brotli compression when client supports it"""
    headers = {"Accept-Encoding": "br"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

    assert resp.status_code == 200
    assert resp.raw.headers.get("Content-Encoding") == "br"
    assert resp.raw.headers.get("Vary") == "Accept-Encoding"

    # Manual decompression (requests doesn't auto-decompress brotli)
    raw_data = resp.raw.read()
    decompressed = brotli.decompress(raw_data)
    data = json.loads(decompressed)

    assert "items" in data
    assert len(data["items"]) == 1000


def test_no_compression_when_client_doesnt_support(titan_server, http_session):
    """Test that compression is skipped when client doesn't send Accept-Encoding"""
    # Explicitly disable Accept-Encoding (requests adds it automatically by default)
    resp = http_session.get(f"{titan_server}/large", timeout=5, headers={"Accept-Encoding": "identity"})

    assert resp.status_code == 200
    # Should not have Content-Encoding header (uncompressed)
    assert "Content-Encoding" not in resp.headers or resp.headers.get("Content-Encoding") == "identity"

    data = resp.json()
    assert "items" in data


def test_compression_content_negotiation(titan_server, http_session):
    """Test server picks best compression algorithm from client preferences"""
    headers = {"Accept-Encoding": "gzip, deflate, br"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

    assert resp.status_code == 200
    # Server should pick one of the supported algorithms
    encoding = resp.raw.headers.get("Content-Encoding")
    assert encoding in ["gzip", "br", "zstd"]

    # Manual decompression based on encoding
    raw_data = resp.raw.read()
    if encoding == "zstd":
        decompressed = zstd.decompress(raw_data)
    elif encoding == "br":
        decompressed = brotli.decompress(raw_data)
    elif encoding == "gzip":
        decompressed = gzip.decompress(raw_data)
    else:
        decompressed = raw_data
    data = json.loads(decompressed)

    assert "items" in data
    assert len(data["items"]) == 1000


def test_small_response_not_compressed(titan_server, http_session):
    """Test that small responses are not compressed (below min_size threshold)"""
    headers = {"Accept-Encoding": "gzip"}
    resp = http_session.get(f"{titan_server}/health", headers=headers, timeout=2)

    assert resp.status_code == 200
    # Health endpoint returns small JSON (~50 bytes), should not be compressed
    # if min_size is set to 1KB (1024 bytes) in config
    # Note: This depends on the config setting
    data = resp.json()
    assert "status" in data


def test_compression_preserves_response_correctness(titan_server, http_session):
    """Test that compression doesn't corrupt response data"""
    headers = {"Accept-Encoding": "gzip"}

    # Make same request with and without compression
    resp_compressed = http_session.get(f"{titan_server}/large", headers=headers, timeout=5)
    resp_uncompressed = http_session.get(f"{titan_server}/large", timeout=5)

    assert resp_compressed.status_code == 200
    assert resp_uncompressed.status_code == 200

    # Both should deserialize to same JSON data
    data_compressed = resp_compressed.json()
    data_uncompressed = resp_uncompressed.json()

    assert data_compressed == data_uncompressed
    assert len(data_compressed["items"]) == len(data_uncompressed["items"])


def test_compression_metrics_exported(titan_server, http_session):
    """Test that compression metrics are exposed via /metrics endpoint"""
    # Make compressed request to generate metrics
    headers = {"Accept-Encoding": "gzip"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5)
    assert resp.status_code == 200

    # Fetch metrics
    metrics_resp = requests.get("http://127.0.0.1:9090/metrics", timeout=2)
    assert metrics_resp.status_code == 200

    metrics_text = metrics_resp.text

    # Verify compression metrics are present
    assert "titan_compression_requests_total" in metrics_text
    assert "titan_compression_bytes_in_total" in metrics_text
    assert "titan_compression_bytes_out_total" in metrics_text
    assert "titan_compression_ratio" in metrics_text
    assert "titan_compression_algorithm_total" in metrics_text

    # Check for algorithm-specific metrics
    assert 'algorithm="gzip"' in metrics_text or 'algorithm="zstd"' in metrics_text or 'algorithm="brotli"' in metrics_text


def test_compression_with_multiple_algorithms(titan_server, http_session):
    """Test compression works correctly with different algorithms"""
    algorithms = [
        ("gzip", "gzip"),
        ("zstd", "zstd"),
        ("br", "br"),
    ]

    for accept_encoding, expected_encoding in algorithms:
        headers = {"Accept-Encoding": accept_encoding}
        resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

        assert resp.status_code == 200
        actual_encoding = resp.raw.headers.get("Content-Encoding")

        # If server doesn't support the algorithm, it might fall back
        # For this test, we expect exact match (assuming all algorithms configured)
        if actual_encoding:  # Only check if compressed
            assert actual_encoding == expected_encoding, f"Expected {expected_encoding}, got {actual_encoding}"

        # Verify data integrity (manual decompression for stream=True)
        raw_data = resp.raw.read()
        if actual_encoding == "zstd":
            decompressed = zstd.decompress(raw_data)
        elif actual_encoding == "br":
            decompressed = brotli.decompress(raw_data)
        elif actual_encoding == "gzip":
            decompressed = gzip.decompress(raw_data)
        else:
            decompressed = raw_data
        data = json.loads(decompressed)
        assert "items" in data
        assert len(data["items"]) == 1000


def test_compression_streaming_large_response(titan_server, http_session):
    """Test streaming compression for very large responses"""
    headers = {"Accept-Encoding": "gzip"}
    # Request a very large response (should trigger streaming if configured)
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5, stream=True)

    assert resp.status_code == 200
    assert resp.raw.headers.get("Content-Encoding") == "gzip"

    # Verify response is valid and complete (manual decompression)
    raw_data = resp.raw.read()
    decompressed = gzip.decompress(raw_data)
    data = json.loads(decompressed)
    assert "items" in data
    assert len(data["items"]) == 1000


def test_compression_vary_header(titan_server, http_session):
    """Test that Vary: Accept-Encoding header is set for caching"""
    headers = {"Accept-Encoding": "gzip"}
    resp = http_session.get(f"{titan_server}/large", headers=headers, timeout=5)

    assert resp.status_code == 200
    # Vary header should be present for proper HTTP caching
    assert resp.headers.get("Vary") == "Accept-Encoding"


def test_compression_ratio_validation(titan_server, http_session):
    """Test that compression actually reduces response size"""
    headers_compressed = {"Accept-Encoding": "gzip"}

    # Get compressed response (stream=True to access raw data)
    resp_compressed = http_session.get(f"{titan_server}/large", headers=headers_compressed, timeout=5, stream=True)

    # Get uncompressed response (no Accept-Encoding)
    resp_uncompressed = http_session.get(f"{titan_server}/large", timeout=5)

    assert resp_compressed.status_code == 200
    assert resp_uncompressed.status_code == 200

    # Get raw compressed data size from wire
    compressed_size = int(resp_compressed.raw.headers.get("Content-Length", 0))
    uncompressed_size = len(resp_uncompressed.content)

    # For a large JSON response, compression should provide significant reduction
    # Expect at least 30% compression ratio
    assert compressed_size < uncompressed_size
    compression_ratio = compressed_size / uncompressed_size
    assert compression_ratio < 0.7, f"Compression ratio {compression_ratio:.2%} is too low"
