#!/usr/bin/env python3
"""
Titan API Gateway - Benchmark Runner
Orchestrates benchmarks and generates reports in multiple formats
"""

import argparse
import json
import subprocess
import sys
import time
import re
import os
from pathlib import Path
from typing import Dict, List, Optional
from datetime import datetime

# ANSI color codes for terminal output
class Colors:
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    RED = '\033[0;31m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    BOLD = '\033[1m'
    NC = '\033[0m'  # No Color


class BenchmarkResult:
    """Benchmark result data structure"""
    def __init__(self, scenario: str):
        self.scenario = scenario
        self.timestamp = datetime.now().isoformat()
        self.duration = 0
        self.requests_total = 0
        self.requests_per_sec = 0
        self.transfer_per_sec = 0
        self.latency_avg = 0
        self.latency_stdev = 0
        self.latency_max = 0
        self.latency_p50 = 0
        self.latency_p75 = 0
        self.latency_p90 = 0
        self.latency_p99 = 0
        self.errors = 0
        self.success_rate = 100.0

    def to_dict(self) -> Dict:
        return {
            'scenario': self.scenario,
            'timestamp': self.timestamp,
            'duration': self.duration,
            'requests_total': self.requests_total,
            'requests_per_sec': self.requests_per_sec,
            'transfer_per_sec': self.transfer_per_sec,
            'latency': {
                'avg': self.latency_avg,
                'stdev': self.latency_stdev,
                'max': self.latency_max,
                'p50': self.latency_p50,
                'p75': self.latency_p75,
                'p90': self.latency_p90,
                'p99': self.latency_p99,
            },
            'errors': self.errors,
            'success_rate': self.success_rate,
        }


class BenchmarkRunner:
    """Main benchmark orchestration class"""

    def __init__(self, config_path: str, duration: int, connections: int, threads: int):
        self.config_path = config_path
        self.duration = duration
        self.connections = connections
        self.threads = threads
        self.titan_process = None
        self.backend_process = None

    def start_titan(self) -> bool:
        """Start Titan server with the specified config"""
        print(f"{Colors.CYAN}→ Starting Titan with config: {self.config_path}{Colors.NC}")

        # Build path to titan binary
        titan_bin = Path(__file__).parent.parent / "build" / "release" / "src" / "titan"

        if not titan_bin.exists():
            print(f"{Colors.RED}✗ Titan binary not found at {titan_bin}{Colors.NC}")
            print(f"{Colors.YELLOW}  Run: make release{Colors.NC}")
            return False

        try:
            self.titan_process = subprocess.Popen(
                [str(titan_bin), "--config", self.config_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            # Give Titan time to start
            time.sleep(2)

            # Check if it's still running
            if self.titan_process.poll() is not None:
                print(f"{Colors.RED}✗ Titan failed to start{Colors.NC}")
                stderr = self.titan_process.stderr.read().decode()
                print(f"{Colors.RED}{stderr}{Colors.NC}")
                return False

            print(f"{Colors.GREEN}✓ Titan started (PID: {self.titan_process.pid}){Colors.NC}")
            return True

        except Exception as e:
            print(f"{Colors.RED}✗ Failed to start Titan: {e}{Colors.NC}")
            return False

    def stop_titan(self):
        """Stop Titan server"""
        if self.titan_process:
            print(f"{Colors.CYAN}→ Stopping Titan...{Colors.NC}")
            self.titan_process.terminate()
            try:
                self.titan_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.titan_process.kill()
            print(f"{Colors.GREEN}✓ Titan stopped{Colors.NC}")

    def run_wrk(self, url: str) -> Optional[str]:
        """Run wrk benchmark"""
        cmd = [
            "wrk",
            "-t", str(self.threads),
            "-c", str(self.connections),
            "-d", f"{self.duration}s",
            "--latency",
            url
        ]

        print(f"{Colors.CYAN}→ Running wrk benchmark...{Colors.NC}")
        print(f"{Colors.CYAN}  Command: {' '.join(cmd)}{Colors.NC}")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=self.duration + 30)
            return result.stdout
        except subprocess.TimeoutExpired:
            print(f"{Colors.RED}✗ wrk benchmark timed out{Colors.NC}")
            return None
        except FileNotFoundError:
            print(f"{Colors.RED}✗ wrk not found{Colors.NC}")
            return None

    def run_h2load(self, url: str) -> Optional[str]:
        """Run h2load benchmark"""
        cmd = [
            "h2load",
            "-t", str(self.threads),
            "-c", str(self.connections),
            "-D", f"{self.duration}",
            url
        ]

        print(f"{Colors.CYAN}→ Running h2load benchmark...{Colors.NC}")
        print(f"{Colors.CYAN}  Command: {' '.join(cmd)}{Colors.NC}")

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=self.duration + 30)
            return result.stdout
        except subprocess.TimeoutExpired:
            print(f"{Colors.RED}✗ h2load benchmark timed out{Colors.NC}")
            return None
        except FileNotFoundError:
            print(f"{Colors.RED}✗ h2load not found{Colors.NC}")
            return None

    def parse_wrk_output(self, output: str, result: BenchmarkResult):
        """Parse wrk output and populate result"""
        # Parse requests/sec
        match = re.search(r'Requests/sec:\s+([\d.]+)', output)
        if match:
            result.requests_per_sec = float(match.group(1))

        # Parse transfer/sec
        match = re.search(r'Transfer/sec:\s+([\d.]+)(\w+)', output)
        if match:
            value = float(match.group(1))
            unit = match.group(2)
            # Convert to MB/s
            if unit == 'KB':
                value /= 1024
            elif unit == 'GB':
                value *= 1024
            result.transfer_per_sec = value

        # Parse latency stats
        match = re.search(r'Latency\s+([\d.]+)(\w+)\s+([\d.]+)(\w+)\s+([\d.]+)(\w+)', output)
        if match:
            result.latency_avg = self._convert_to_ms(float(match.group(1)), match.group(2))
            result.latency_stdev = self._convert_to_ms(float(match.group(3)), match.group(4))
            result.latency_max = self._convert_to_ms(float(match.group(5)), match.group(6))

        # Parse percentiles
        percentile_section = re.search(r'Latency Distribution.*?(\d+)%\s+([\d.]+)(\w+).*?(\d+)%\s+([\d.]+)(\w+).*?(\d+)%\s+([\d.]+)(\w+).*?(\d+)%\s+([\d.]+)(\w+)', output, re.DOTALL)
        if percentile_section:
            result.latency_p50 = self._convert_to_ms(float(percentile_section.group(2)), percentile_section.group(3))
            result.latency_p75 = self._convert_to_ms(float(percentile_section.group(5)), percentile_section.group(6))
            result.latency_p90 = self._convert_to_ms(float(percentile_section.group(8)), percentile_section.group(9))
            result.latency_p99 = self._convert_to_ms(float(percentile_section.group(11)), percentile_section.group(12))

        # Parse total requests
        match = re.search(r'(\d+) requests in', output)
        if match:
            result.requests_total = int(match.group(1))

        # Parse errors
        match = re.search(r'Non-2xx or 3xx responses: (\d+)', output)
        if match:
            result.errors = int(match.group(1))
            if result.requests_total > 0:
                result.success_rate = ((result.requests_total - result.errors) / result.requests_total) * 100

        result.duration = self.duration

    def parse_h2load_output(self, output: str, result: BenchmarkResult):
        """Parse h2load output and populate result"""
        # Parse requests/sec
        match = re.search(r'finished in.*?(\d+) req/s', output)
        if match:
            result.requests_per_sec = float(match.group(1))

        # Parse total requests
        match = re.search(r'requests: (\d+) total', output)
        if match:
            result.requests_total = int(match.group(1))

        # Parse latency (h2load reports in microseconds)
        match = re.search(r'time for request:\s+min=([\d.]+)(\w+)\s+max=([\d.]+)(\w+)\s+mean=([\d.]+)(\w+)\s+sd=([\d.]+)(\w+)', output)
        if match:
            result.latency_avg = self._convert_to_ms(float(match.group(5)), match.group(6))
            result.latency_stdev = self._convert_to_ms(float(match.group(7)), match.group(8))
            result.latency_max = self._convert_to_ms(float(match.group(3)), match.group(4))

        result.duration = self.duration

    def _convert_to_ms(self, value: float, unit: str) -> float:
        """Convert latency to milliseconds"""
        if unit == 'us':
            return value / 1000
        elif unit == 'ms':
            return value
        elif unit == 's':
            return value * 1000
        return value

    def run_benchmark(self, scenario: str, url: str = "http://localhost:8080/api", use_h2load: bool = False) -> Optional[BenchmarkResult]:
        """Run a complete benchmark"""
        result = BenchmarkResult(scenario)

        # Start Titan
        if not self.start_titan():
            return None

        try:
            # Wait for server to be ready
            time.sleep(1)

            # Run benchmark
            if use_h2load:
                output = self.run_h2load(url)
                if output:
                    self.parse_h2load_output(output, result)
            else:
                output = self.run_wrk(url)
                if output:
                    self.parse_wrk_output(output, result)

            if output:
                print(f"\n{Colors.GREEN}✓ Benchmark completed{Colors.NC}")
                self._print_result(result)
                return result
            else:
                return None

        finally:
            self.stop_titan()

    def _print_result(self, result: BenchmarkResult):
        """Print benchmark result to terminal"""
        print(f"\n{Colors.BOLD}=== Benchmark Results: {result.scenario} ==={Colors.NC}")
        print(f"Duration:        {result.duration}s")
        print(f"Total Requests:  {result.requests_total:,}")
        print(f"Throughput:      {result.requests_per_sec:,.2f} req/s")
        print(f"Transfer:        {result.transfer_per_sec:.2f} MB/s")
        print(f"\n{Colors.BOLD}Latency:{Colors.NC}")
        print(f"  Average:       {result.latency_avg:.2f} ms")
        print(f"  Stdev:         {result.latency_stdev:.2f} ms")
        print(f"  Max:           {result.latency_max:.2f} ms")
        if result.latency_p50 > 0:
            print(f"  p50:           {result.latency_p50:.2f} ms")
            print(f"  p75:           {result.latency_p75:.2f} ms")
            print(f"  p90:           {result.latency_p90:.2f} ms")
            print(f"  p99:           {result.latency_p99:.2f} ms")
        print(f"\n{Colors.BOLD}Errors:{Colors.NC}")
        print(f"  Total:         {result.errors:,}")
        print(f"  Success Rate:  {result.success_rate:.2f}%")
        print()


def save_json_report(result: BenchmarkResult, output_path: str):
    """Save benchmark result as JSON"""
    with open(output_path, 'w') as f:
        json.dump(result.to_dict(), f, indent=2)
    print(f"{Colors.GREEN}✓ JSON report saved: {output_path}{Colors.NC}")


def generate_markdown_report(results: List[BenchmarkResult], output_path: str):
    """Generate markdown report from multiple results"""
    md = f"# Titan Benchmark Report\n\n"
    md += f"**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n"

    md += "## Summary\n\n"
    md += "| Scenario | Throughput (req/s) | Avg Latency (ms) | p99 Latency (ms) | Success Rate |\n"
    md += "|----------|------------------:|------------------:|------------------:|--------------:|\n"

    for result in results:
        md += f"| {result.scenario} | {result.requests_per_sec:,.2f} | {result.latency_avg:.2f} | {result.latency_p99:.2f} | {result.success_rate:.2f}% |\n"

    md += "\n## Detailed Results\n\n"

    for result in results:
        md += f"### {result.scenario}\n\n"
        md += f"- **Duration:** {result.duration}s\n"
        md += f"- **Total Requests:** {result.requests_total:,}\n"
        md += f"- **Throughput:** {result.requests_per_sec:,.2f} req/s\n"
        md += f"- **Transfer:** {result.transfer_per_sec:.2f} MB/s\n\n"
        md += "**Latency:**\n"
        md += f"- Average: {result.latency_avg:.2f} ms\n"
        md += f"- Stdev: {result.latency_stdev:.2f} ms\n"
        md += f"- Max: {result.latency_max:.2f} ms\n"
        if result.latency_p50 > 0:
            md += f"- p50: {result.latency_p50:.2f} ms\n"
            md += f"- p75: {result.latency_p75:.2f} ms\n"
            md += f"- p90: {result.latency_p90:.2f} ms\n"
            md += f"- p99: {result.latency_p99:.2f} ms\n"
        md += f"\n**Errors:** {result.errors:,} ({result.success_rate:.2f}% success rate)\n\n"

    with open(output_path, 'w') as f:
        f.write(md)

    print(f"{Colors.GREEN}✓ Markdown report saved: {output_path}{Colors.NC}")


def main():
    parser = argparse.ArgumentParser(description='Titan Benchmark Runner')
    parser.add_argument('--scenario', type=str, help='Benchmark scenario name')
    parser.add_argument('--config', type=str, help='Titan config file path')
    parser.add_argument('--duration', type=int, default=30, help='Benchmark duration in seconds')
    parser.add_argument('--connections', type=int, default=100, help='Number of connections')
    parser.add_argument('--threads', type=int, default=4, help='Number of threads')
    parser.add_argument('--output', type=str, help='Output JSON file path')
    parser.add_argument('--url', type=str, default='http://localhost:8080/api', help='Benchmark URL')
    parser.add_argument('--h2load', action='store_true', help='Use h2load instead of wrk')
    parser.add_argument('--report', type=str, help='Generate report from results directory')

    args = parser.parse_args()

    # Report generation mode
    if args.report:
        results_dir = Path(args.report)
        if not results_dir.exists():
            print(f"{Colors.RED}✗ Results directory not found: {results_dir}{Colors.NC}")
            sys.exit(1)

        # Load all JSON results
        results = []
        for json_file in results_dir.glob('bench-*.json'):
            with open(json_file) as f:
                data = json.load(f)
                result = BenchmarkResult(data['scenario'])
                result.timestamp = data['timestamp']
                result.duration = data['duration']
                result.requests_total = data['requests_total']
                result.requests_per_sec = data['requests_per_sec']
                result.transfer_per_sec = data['transfer_per_sec']
                result.latency_avg = data['latency']['avg']
                result.latency_stdev = data['latency']['stdev']
                result.latency_max = data['latency']['max']
                result.latency_p50 = data['latency']['p50']
                result.latency_p75 = data['latency']['p75']
                result.latency_p90 = data['latency']['p90']
                result.latency_p99 = data['latency']['p99']
                result.errors = data['errors']
                result.success_rate = data['success_rate']
                results.append(result)

        if not results:
            print(f"{Colors.YELLOW}⚠ No benchmark results found in {results_dir}{Colors.NC}")
            sys.exit(0)

        # Generate reports
        generate_markdown_report(results, results_dir / 'benchmark_report.md')
        print(f"{Colors.GREEN}✅ Report generation complete{Colors.NC}")
        sys.exit(0)

    # Benchmark execution mode
    if not args.scenario or not args.config or not args.output:
        parser.print_help()
        sys.exit(1)

    runner = BenchmarkRunner(args.config, args.duration, args.connections, args.threads)
    result = runner.run_benchmark(args.scenario, args.url, args.h2load)

    if result:
        save_json_report(result, args.output)
        sys.exit(0)
    else:
        print(f"{Colors.RED}✗ Benchmark failed{Colors.NC}")
        sys.exit(1)


if __name__ == '__main__':
    main()
