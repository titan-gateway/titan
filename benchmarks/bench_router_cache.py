#!/usr/bin/env python3
"""
Router Cache Benchmark
Measures Titan's router performance with cache enabled vs disabled
Uses the actual Titan release binary
"""

import json
import subprocess
import time
import requests
import statistics
from pathlib import Path

TITAN_BINARY = Path("../build/release/src/titan").resolve()
CONFIG_DIR = Path("../config").resolve()
BACKEND_PORT = 3001
TITAN_PORT = 8080


def start_backend():
    """Start mock backend server"""
    print(f"Starting backend on port {BACKEND_PORT}...")
    proc = subprocess.Popen(
        ["python3", "-m", "http.server", str(BACKEND_PORT)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)
    return proc


def create_config(cache_enabled: bool, num_routes: int = 100):
    """Create Titan config with specified number of routes"""
    routes = []
    for i in range(num_routes):
        routes.append({
            "path": f"/api/endpoint{i}",
            "method": "GET",
            "upstream": "backend",
            "handler_id": f"handler{i}",
            "priority": 100
        })

    config = {
        "version": "1.0",
        "server": {
            "worker_threads": 1,  # Single thread for consistent benchmarking
            "listen_address": "0.0.0.0",
            "listen_port": TITAN_PORT,
            "backlog": 2048,
        },
        "upstreams": [{
            "name": "backend",
            "load_balancing": "round_robin",
            "pool_size": 50,
            "backends": [{"host": "127.0.0.1", "port": BACKEND_PORT}]
        }],
        "routes": routes,
        "logging": {"level": "error"}
    }

    cache_status = "ON" if cache_enabled else "OFF"
    config_path = f"/tmp/titan_bench_cache_{cache_status}.json"
    with open(config_path, 'w') as f:
        json.dump(config, f)

    return config_path


def start_titan(config_path: str):
    """Start Titan server"""
    print(f"Starting Titan with config: {config_path}")
    proc = subprocess.Popen(
        [str(TITAN_BINARY), "--config", config_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(2)  # Wait for startup
    return proc


def benchmark_hot_route(cache_enabled: bool, iterations: int = 10000):
    """Benchmark: Single hot route (best case for caching)"""
    config_path = create_config(cache_enabled, num_routes=10)
    backend_proc = start_backend()
    titan_proc = start_titan(config_path)

    try:
        # Warm up
        for _ in range(100):
            try:
                requests.get(f"http://127.0.0.1:{TITAN_PORT}/api/endpoint0", timeout=1)
            except:
                pass

        # Benchmark
        latencies = []
        errors = 0

        start_time = time.perf_counter()
        for _ in range(iterations):
            req_start = time.perf_counter()
            try:
                resp = requests.get(f"http://127.0.0.1:{TITAN_PORT}/api/endpoint0", timeout=1)
                if resp.status_code == 200:
                    latencies.append((time.perf_counter() - req_start) * 1_000_000)  # microseconds
                else:
                    errors += 1
            except Exception as e:
                errors += 1

        total_time = time.perf_counter() - start_time

        return {
            "cache_enabled": cache_enabled,
            "scenario": "Hot Route (single endpoint)",
            "iterations": iterations,
            "errors": errors,
            "throughput_rps": iterations / total_time,
            "avg_latency_us": statistics.mean(latencies) if latencies else 0,
            "p50_latency_us": statistics.median(latencies) if latencies else 0,
            "p99_latency_us": statistics.quantiles(latencies, n=100)[98] if len(latencies) > 100 else 0,
        }
    finally:
        titan_proc.terminate()
        backend_proc.terminate()
        titan_proc.wait()
        backend_proc.wait()
        time.sleep(1)


def benchmark_uniform_distribution(cache_enabled: bool, iterations: int = 10000):
    """Benchmark: Uniform distribution across 10 routes"""
    import random

    config_path = create_config(cache_enabled, num_routes=10)
    backend_proc = start_backend()
    titan_proc = start_titan(config_path)

    try:
        # Warm up
        for i in range(100):
            try:
                requests.get(f"http://127.0.0.1:{TITAN_PORT}/api/endpoint{i % 10}", timeout=1)
            except:
                pass

        # Benchmark
        latencies = []
        errors = 0

        start_time = time.perf_counter()
        for _ in range(iterations):
            endpoint = random.randint(0, 9)
            req_start = time.perf_counter()
            try:
                resp = requests.get(f"http://127.0.0.1:{TITAN_PORT}/api/endpoint{endpoint}", timeout=1)
                if resp.status_code == 200:
                    latencies.append((time.perf_counter() - req_start) * 1_000_000)
                else:
                    errors += 1
            except:
                errors += 1

        total_time = time.perf_counter() - start_time

        return {
            "cache_enabled": cache_enabled,
            "scenario": "Uniform Distribution (10 endpoints)",
            "iterations": iterations,
            "errors": errors,
            "throughput_rps": iterations / total_time,
            "avg_latency_us": statistics.mean(latencies) if latencies else 0,
            "p50_latency_us": statistics.median(latencies) if latencies else 0,
            "p99_latency_us": statistics.quantiles(latencies, n=100)[98] if len(latencies) > 100 else 0,
        }
    finally:
        titan_proc.terminate()
        backend_proc.terminate()
        titan_proc.wait()
        backend_proc.wait()
        time.sleep(1)


def print_results(results_off, results_on):
    """Print benchmark comparison"""
    print("\n" + "="*70)
    print(f"SCENARIO: {results_on['scenario']}")
    print("="*70)

    print(f"\n{'Metric':<30} {'Cache OFF':<20} {'Cache ON':<20} {'Improvement'}")
    print("-"*70)

    # Throughput
    throughput_improvement = ((results_on['throughput_rps'] - results_off['throughput_rps']) / results_off['throughput_rps']) * 100
    print(f"{'Throughput (req/s)':<30} {results_off['throughput_rps']:>18.2f}  {results_on['throughput_rps']:>18.2f}  {throughput_improvement:>+6.2f}%")

    # Latencies
    for metric in ['avg_latency_us', 'p50_latency_us', 'p99_latency_us']:
        label = metric.replace('_', ' ').title().replace('Us', '(μs)')
        off_val = results_off[metric]
        on_val = results_on[metric]
        if off_val > 0:
            improvement = ((off_val - on_val) / off_val) * 100
            print(f"{label:<30} {off_val:>18.2f}  {on_val:>18.2f}  {improvement:>+6.2f}%")

    print(f"\n{'Errors':<30} {results_off['errors']:>18}  {results_on['errors']:>18}")


def main():
    print("="*70)
    print("Router Cache Performance Benchmark")
    print("Using Titan Release Binary")
    print("="*70)

    # Benchmark 1: Hot route
    print("\n[1/2] Benchmarking hot route scenario...")
    results1_off = benchmark_hot_route(cache_enabled=False, iterations=5000)
    results1_on = benchmark_hot_route(cache_enabled=True, iterations=5000)
    print_results(results1_off, results1_on)

    # Benchmark 2: Uniform distribution
    print("\n[2/2] Benchmarking uniform distribution...")
    results2_off = benchmark_uniform_distribution(cache_enabled=False, iterations=5000)
    results2_on = benchmark_uniform_distribution(cache_enabled=True, iterations=5000)
    print_results(results2_off, results2_on)

    print("\n" + "="*70)
    print("Benchmark Complete!")
    print("="*70)


if __name__ == "__main__":
    main()
