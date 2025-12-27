#!/usr/bin/env python3
"""
Analyze headless PowerProfile benchmarks with power profiling data

Parses benchmark logs and powermetrics plist files to correlate
performance metrics with power consumption across different entity counts.
"""

import sys
import re
from pathlib import Path
from typing import Dict, List, Tuple
import statistics


def parse_benchmark_log(file_path: str) -> Dict[str, float]:
    """Extract performance metrics from benchmark log"""
    stats = {}

    try:
        with open(file_path, 'r') as f:
            content = f.read()

        # Extract entity count
        entity_match = re.search(r'Entity Count:\s+(\d+)', content)
        if entity_match:
            stats['entity_count'] = int(entity_match.group(1))

        # Extract threading mode
        threading_match = re.search(r'Threading Mode:\s+(\w+)', content)
        if threading_match:
            stats['threading_mode'] = threading_match.group(1)

        # Extract benchmark results
        frames_match = re.search(r'Total Frames:\s+(\d+)', content)
        if frames_match:
            stats['total_frames'] = int(frames_match.group(1))

        time_match = re.search(r'Total Time:\s+(\d+)\s+ms', content)
        if time_match:
            stats['total_time_ms'] = int(time_match.group(1))

        frame_time_match = re.search(r'Avg Frame Time:\s+([\d.]+)\s+ms', content)
        if frame_time_match:
            stats['avg_frame_time_ms'] = float(frame_time_match.group(1))

        fps_match = re.search(r'Avg FPS:\s+([\d.]+)', content)
        if fps_match:
            stats['avg_fps'] = float(fps_match.group(1))

        workers_match = re.search(r'Workers Active:\s+(\d+)', content)
        if workers_match:
            stats['workers_active'] = int(workers_match.group(1))

        return stats
    except Exception as e:
        print(f"Error parsing benchmark log {file_path}: {e}")
        return {}


def extract_combined_power_samples(plist_file: str) -> List[float]:
    """Extract combined power samples from powermetrics plist"""
    samples = []
    try:
        with open(plist_file, 'r') as f:
            for line in f:
                if line.startswith('Combined Power'):
                    parts = line.split(': ')
                    if len(parts) == 2:
                        power_str = parts[1].replace(' mW', '')
                        try:
                            mW = float(power_str)
                            samples.append(mW / 1000)  # Convert to watts
                        except:
                            pass
    except Exception as e:
        print(f"Error reading plist {plist_file}: {e}")

    return samples


def analyze_power(samples: List[float]) -> Dict[str, float]:
    """Calculate power statistics"""
    if not samples:
        return {}

    return {
        'avg_power_W': statistics.mean(samples),
        'min_power_W': min(samples),
        'max_power_W': max(samples),
        'stdev_power_W': statistics.stdev(samples) if len(samples) > 1 else 0,
        'sample_count': len(samples)
    }


def print_benchmark_analysis(benchmark_file: str, plist_file: str):
    """Analyze and print headless benchmark results"""

    # Parse benchmark log
    bench_stats = parse_benchmark_log(benchmark_file)
    if not bench_stats:
        print(f"Could not parse benchmark log: {benchmark_file}")
        return

    # Extract power data
    power_samples = extract_combined_power_samples(plist_file)
    power_stats = analyze_power(power_samples)

    # Display results
    print(f"\n{'='*70}")
    print(f"Headless Benchmark Analysis")
    print(f"{'='*70}")

    print(f"\nPerformance Metrics:")
    print(f"  Entity Count:       {bench_stats.get('entity_count', '?'):,}")
    print(f"  Threading Mode:     {bench_stats.get('threading_mode', '?')}")
    print(f"  Active Workers:     {bench_stats.get('workers_active', '?')}")
    print(f"  Total Frames:       {bench_stats.get('total_frames', '?'):,}")
    print(f"  Total Time:         {bench_stats.get('total_time_ms', '?')/1000:.1f}s")
    print(f"  Avg Frame Time:     {bench_stats.get('avg_frame_time_ms', '?'):.2f}ms")
    print(f"  Avg FPS:            {bench_stats.get('avg_fps', '?'):.1f}")

    if power_stats:
        print(f"\nPower Consumption:")
        print(f"  Average:            {power_stats.get('avg_power_W', 0):.2f}W")
        print(f"  Min:                {power_stats.get('min_power_W', 0):.2f}W")
        print(f"  Max:                {power_stats.get('max_power_W', 0):.2f}W")
        print(f"  Std Dev:            {power_stats.get('stdev_power_W', 0):.2f}W")
        print(f"  Samples:            {power_stats.get('sample_count', 0)}")

        # Calculate efficiency metrics
        entity_count = bench_stats.get('entity_count', 1)
        avg_fps = bench_stats.get('avg_fps', 1)
        avg_power = power_stats.get('avg_power_W', 1)

        print(f"\nEfficiency Metrics:")
        print(f"  Power per entity:   {(avg_power * 1000) / entity_count:.3f}mW/entity")
        print(f"  Power per FPS:      {avg_power / avg_fps:.2f}W/FPS")
        print(f"  Throughput:         {(entity_count * avg_fps):,.0f} entity-updates/sec")

        # Battery estimate
        battery_wh = 70
        hours = battery_wh / avg_power if avg_power > 0 else 0
        print(f"\nBattery Life (70Wh):")
        print(f"  Continuous:         {hours:.1f} hours")

    print(f"\n{'='*70}\n")


def main():
    if len(sys.argv) < 2:
        print("Usage: analyze_benchmark.py <benchmark_log.txt>")
        print("  The script will look for matching .plist file in the same directory")
        sys.exit(1)

    benchmark_file = sys.argv[1]

    # Find corresponding plist file
    benchmark_path = Path(benchmark_file)
    if not benchmark_path.exists():
        print(f"Benchmark log not found: {benchmark_file}")
        sys.exit(1)

    # Construct plist filename from benchmark filename
    # e.g., benchmark_multi_50000_20251225_064755.txt → power_multi_50000_20251225_064755.plist
    parts = benchmark_path.name.split('_')
    if parts[0] == 'benchmark' and len(parts) >= 4:
        plist_name = 'power_' + '_'.join(parts[1:]).replace('.txt', '.plist')
        plist_file = benchmark_path.parent / plist_name
    else:
        print(f"Cannot determine plist filename from: {benchmark_file}")
        sys.exit(1)

    if not plist_file.exists():
        print(f"Power data not found: {plist_file}")
        sys.exit(1)

    print_benchmark_analysis(str(benchmark_file), str(plist_file))


if __name__ == '__main__':
    main()
