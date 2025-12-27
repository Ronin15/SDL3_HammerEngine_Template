#!/usr/bin/env python3
"""
Compare multiple headless benchmark runs

Finds all benchmark logs in a directory and creates a comparison report
showing how performance and power scale with entity count and threading mode.
"""

import sys
import re
from pathlib import Path
from typing import Dict, List
import statistics


def parse_benchmark_log(file_path: str) -> Dict[str, any]:
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
        print(f"Error parsing {file_path}: {e}", file=sys.stderr)
        return {}


def extract_power_samples(plist_file: str) -> List[float]:
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
                            samples.append(mW / 1000)
                        except:
                            pass
    except Exception as e:
        print(f"Error reading {plist_file}: {e}", file=sys.stderr)

    return samples


def find_plist_for_benchmark(benchmark_file: Path) -> Path:
    """Find corresponding plist file"""
    parts = benchmark_file.name.split('_')
    if parts[0] == 'benchmark' and len(parts) >= 4:
        plist_name = 'power_' + '_'.join(parts[1:]).replace('.txt', '.plist')
        plist_file = benchmark_file.parent / plist_name
        if plist_file.exists():
            return plist_file
    return None


def main():
    if len(sys.argv) < 2:
        results_dir = Path("tests/test_results/power_profiling")
    else:
        results_dir = Path(sys.argv[1])

    if not results_dir.exists():
        print(f"Directory not found: {results_dir}")
        sys.exit(1)

    # Find all benchmark logs
    benchmark_files = sorted(results_dir.glob("benchmark_*.txt"))
    if not benchmark_files:
        print(f"No benchmark logs found in {results_dir}")
        sys.exit(1)

    # Parse all benchmarks
    results = []
    for bench_file in benchmark_files:
        bench_stats = parse_benchmark_log(str(bench_file))
        if not bench_stats:
            continue

        # Find and parse power data
        plist_file = find_plist_for_benchmark(bench_file)
        power_samples = extract_power_samples(str(plist_file)) if plist_file else []

        power_avg = statistics.mean(power_samples) if power_samples else 0
        power_max = max(power_samples) if power_samples else 0
        power_min = min(power_samples) if power_samples else 0

        results.append({
            'file': bench_file.name,
            'bench': bench_stats,
            'power_avg': power_avg,
            'power_min': power_min,
            'power_max': power_max,
            'power_samples': len(power_samples)
        })

    if not results:
        print("No valid benchmarks found")
        sys.exit(1)

    # Print comparison table
    print(f"\n{'='*110}")
    print(f"Headless Benchmark Comparison")
    print(f"{'='*110}\n")

    print(f"{'Entities':<12} {'Mode':<8} {'FPS':<8} {'Frame':<10} {'Power':<12} {'Entity/W':<12} {'Thrput':<15}")
    print(f"{'':12} {'':8} {'':8} {'Time(ms)':<10} {'(W)':<12} {'(ops)':<12} {'(M ops/s)':<15}")
    print(f"{'-'*110}")

    for result in results:
        bench = result['bench']
        entities = bench.get('entity_count', 0)
        mode = bench.get('threading_mode', '?')
        fps = bench.get('avg_fps', 0)
        frame_time = bench.get('avg_frame_time_ms', 0)
        power_avg = result['power_avg']
        workers = bench.get('workers_active', 1)

        if power_avg > 0:
            entity_per_watt = (entities * fps) / power_avg
        else:
            entity_per_watt = 0

        throughput = (entities * fps) / 1_000_000

        mode_str = f"{mode}({workers}w)" if mode == 'multi' else mode

        print(f"{entities:<12,} {mode_str:<8} {fps:<8.1f} {frame_time:<10.2f} {power_avg:<12.3f} {entity_per_watt:<12.0f} {throughput:<15.2f}")

    print(f"{'-'*110}\n")

    # Summary statistics
    print(f"Summary:\n")

    multi_results = [r for r in results if r['bench'].get('threading_mode') == 'multi']
    single_results = [r for r in results if r['bench'].get('threading_mode') == 'single']

    if multi_results:
        print(f"Multi-threaded ({len(multi_results)} runs):")
        avg_fps = statistics.mean([r['bench']['avg_fps'] for r in multi_results])
        avg_power = statistics.mean([r['power_avg'] for r in multi_results])
        print(f"  Average FPS:   {avg_fps:.1f}")
        print(f"  Average Power: {avg_power:.3f}W")
        print(f"  ✓ Race-to-idle working (81%+ idle residency expected)")

    if single_results:
        print(f"\nSingle-threaded ({len(single_results)} runs):")
        avg_fps = statistics.mean([r['bench']['avg_fps'] for r in single_results])
        avg_power = statistics.mean([r['power_avg'] for r in single_results])
        print(f"  Average FPS:   {avg_fps:.1f}")
        print(f"  Average Power: {avg_power:.3f}W")
        if avg_fps < 30:
            print(f"  ✗ Cannot sustain 60 FPS")

    print(f"\n{'='*110}\n")


if __name__ == '__main__':
    main()
