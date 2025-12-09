#!/usr/bin/env python3
"""
Titan API Gateway - Benchmark Comparison Tool
Compares benchmark results before/after optimizations
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict

# ANSI color codes
class Colors:
    GREEN = '\033[0;32m'
    RED = '\033[0;31m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    BOLD = '\033[1m'
    NC = '\033[0m'


def load_result(file_path: str) -> Dict:
    """Load benchmark result from JSON file"""
    try:
        with open(file_path, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"{Colors.RED}✗ File not found: {file_path}{Colors.NC}")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"{Colors.RED}✗ Invalid JSON file: {file_path}{Colors.NC}")
        sys.exit(1)


def calculate_improvement(before: float, after: float) -> tuple:
    """Calculate improvement percentage and determine if it's better or worse"""
    if before == 0:
        return 0.0, True

    improvement = ((after - before) / before) * 100
    return improvement, improvement > 0


def format_improvement(improvement: float, is_better: bool, higher_is_better: bool = True) -> str:
    """Format improvement with color coding"""
    # For latency metrics, lower is better
    if not higher_is_better:
        is_better = not is_better

    if abs(improvement) < 0.1:
        color = Colors.YELLOW
        symbol = "→"
    elif is_better:
        color = Colors.GREEN
        symbol = "↑" if higher_is_better else "↓"
    else:
        color = Colors.RED
        symbol = "↓" if higher_is_better else "↑"

    return f"{color}{symbol} {abs(improvement):+.2f}%{Colors.NC}"


def compare_metrics(before: Dict, after: Dict, metric_name: str, higher_is_better: bool = True) -> str:
    """Compare a specific metric between before and after"""
    before_val = before.get(metric_name, 0)
    after_val = after.get(metric_name, 0)
    improvement, is_better = calculate_improvement(before_val, after_val)
    return format_improvement(improvement, is_better, higher_is_better)


def compare_latency_metric(before: Dict, after: Dict, metric_name: str) -> str:
    """Compare a latency metric (lower is better)"""
    before_val = before['latency'].get(metric_name, 0)
    after_val = after['latency'].get(metric_name, 0)
    improvement, is_better = calculate_improvement(before_val, after_val)
    # For latency, improvement means reduction (negative change is good)
    return format_improvement(improvement, is_better, higher_is_better=False)


def print_comparison(before: Dict, after: Dict):
    """Print detailed comparison of benchmark results"""
    scenario = after.get('scenario', 'Unknown')

    print(f"\n{Colors.BOLD}{'='*80}{Colors.NC}")
    print(f"{Colors.BOLD}Benchmark Comparison: {scenario}{Colors.NC}")
    print(f"{Colors.BOLD}{'='*80}{Colors.NC}\n")

    print(f"{Colors.CYAN}Before:{Colors.NC} {before.get('timestamp', 'Unknown')}")
    print(f"{Colors.CYAN}After:{Colors.NC}  {after.get('timestamp', 'Unknown')}\n")

    # Throughput comparison
    print(f"{Colors.BOLD}Throughput:{Colors.NC}")
    before_rps = before.get('requests_per_sec', 0)
    after_rps = after.get('requests_per_sec', 0)
    improvement = compare_metrics(before, after, 'requests_per_sec', higher_is_better=True)
    print(f"  Requests/sec:  {before_rps:>12,.2f} → {after_rps:>12,.2f}  {improvement}")

    before_transfer = before.get('transfer_per_sec', 0)
    after_transfer = after.get('transfer_per_sec', 0)
    transfer_improvement = compare_metrics(before, after, 'transfer_per_sec', higher_is_better=True)
    print(f"  Transfer/sec:  {before_transfer:>12.2f} → {after_transfer:>12.2f} MB/s  {transfer_improvement}\n")

    # Latency comparison
    print(f"{Colors.BOLD}Latency:{Colors.NC}")
    metrics = [
        ('avg', 'Average'),
        ('stdev', 'Stdev'),
        ('max', 'Max'),
        ('p50', 'p50'),
        ('p75', 'p75'),
        ('p90', 'p90'),
        ('p99', 'p99'),
    ]

    for metric_key, metric_label in metrics:
        before_lat = before['latency'].get(metric_key, 0)
        after_lat = after['latency'].get(metric_key, 0)
        if before_lat > 0 or after_lat > 0:
            improvement = compare_latency_metric(before, after, metric_key)
            print(f"  {metric_label:10s} {before_lat:>12.2f} → {after_lat:>12.2f} ms  {improvement}")

    # Error comparison
    print(f"\n{Colors.BOLD}Reliability:{Colors.NC}")
    before_errors = before.get('errors', 0)
    after_errors = after.get('errors', 0)
    before_success = before.get('success_rate', 100)
    after_success = after.get('success_rate', 100)

    # For errors, lower is better
    if before_errors > 0 or after_errors > 0:
        error_improvement, _ = calculate_improvement(before_errors, after_errors)
        error_color = Colors.GREEN if error_improvement < 0 else Colors.RED
        print(f"  Errors:        {before_errors:>12,} → {after_errors:>12,}  {error_color}{error_improvement:+.2f}%{Colors.NC}")

    success_improvement = compare_metrics(before, after, 'success_rate', higher_is_better=True)
    print(f"  Success Rate:  {before_success:>11.2f}% → {after_success:>11.2f}%  {success_improvement}")

    # Summary
    print(f"\n{Colors.BOLD}{'='*80}{Colors.NC}")
    rps_pct, rps_better = calculate_improvement(before_rps, after_rps)
    p99_val_before = before['latency'].get('p99', 0)
    p99_val_after = after['latency'].get('p99', 0)
    p99_pct, p99_better = calculate_improvement(p99_val_before, p99_val_after)
    p99_better = not p99_better  # For latency, lower is better

    if abs(rps_pct) < 1 and abs(p99_pct) < 1:
        print(f"{Colors.YELLOW}⚠ No significant performance change detected{Colors.NC}")
    elif rps_better or p99_better:
        print(f"{Colors.GREEN}✅ Performance improved!{Colors.NC}")
        if rps_better:
            print(f"   • Throughput increased by {rps_pct:.2f}%")
        if p99_better:
            print(f"   • p99 latency decreased by {abs(p99_pct):.2f}%")
    else:
        print(f"{Colors.RED}⚠ Performance degraded{Colors.NC}")
        if not rps_better and abs(rps_pct) > 1:
            print(f"   • Throughput decreased by {abs(rps_pct):.2f}%")
        if not p99_better and abs(p99_pct) > 1:
            print(f"   • p99 latency increased by {p99_pct:.2f}%")

    print(f"{Colors.BOLD}{'='*80}{Colors.NC}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Compare Titan benchmark results',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Compare two benchmark files
  ./scripts/compare_results.py results/before.json results/after.json

  # Use with make
  make bench-compare BEFORE=results/before.json AFTER=results/after.json
        """
    )
    parser.add_argument('before', type=str, help='Benchmark results before optimization')
    parser.add_argument('after', type=str, help='Benchmark results after optimization')

    args = parser.parse_args()

    # Load results
    before = load_result(args.before)
    after = load_result(args.after)

    # Validate that scenarios match (optional warning)
    if before.get('scenario') != after.get('scenario'):
        print(f"{Colors.YELLOW}⚠ Warning: Different scenarios being compared:{Colors.NC}")
        print(f"  Before: {before.get('scenario', 'Unknown')}")
        print(f"  After:  {after.get('scenario', 'Unknown')}")

    # Print comparison
    print_comparison(before, after)


if __name__ == '__main__':
    main()
