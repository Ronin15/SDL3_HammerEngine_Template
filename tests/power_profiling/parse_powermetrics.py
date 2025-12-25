#!/usr/bin/env python3
"""
PowerMetrics Parser - Extract and analyze CPU power metrics from macOS powermetrics output

This script parses plist/JSON output from macOS powermetrics and extracts:
- Average CPU package power (W)
- GPU power (should be minimal for 2D games)
- CPU idle residency (C-state percentages)
- Energy consumption estimates
"""

import sys
import json
import plistlib
import re
from pathlib import Path
from typing import Dict, List, Tuple
import statistics

def parse_text_format(file_path: str) -> dict:
    """Parse text format powermetrics output from macOS"""
    data = {
        'cpu_frequencies': [],
        'active_residencies': [],
        'idle_residencies': []
    }

    try:
        with open(file_path, 'r') as f:
            content = f.read()

        # Extract CPU frequency readings (allow flexible spacing)
        freq_pattern = r'CPU (\d+) frequency:\s+([\d.]+) MHz'
        for match in re.finditer(freq_pattern, content):
            cpu_num = int(match.group(1))
            freq_mhz = float(match.group(2))
            data['cpu_frequencies'].append((cpu_num, freq_mhz))

        # Extract active residency percentages (allow flexible spacing)
        active_pattern = r'CPU (\d+) active residency:\s+([\d.]+)%'
        for match in re.finditer(active_pattern, content):
            cpu_num = int(match.group(1))
            active_pct = float(match.group(2))
            data['active_residencies'].append((cpu_num, active_pct))

        # Extract idle residency percentages (allow flexible spacing)
        idle_pattern = r'CPU (\d+) idle residency:\s+([\d.]+)%'
        for match in re.finditer(idle_pattern, content):
            cpu_num = int(match.group(1))
            idle_pct = float(match.group(2))
            data['idle_residencies'].append((cpu_num, idle_pct))

        # If we found any data, it's valid
        if data['cpu_frequencies'] or data['active_residencies'] or data['idle_residencies']:
            return data
        else:
            print(f"Warning: No powermetrics data found in {file_path}")
            return {}

    except Exception as e:
        print(f"Error parsing text format {file_path}: {e}")
        return {}

def parse_plist(file_path: str) -> dict:
    """Parse a plist file from powermetrics"""
    try:
        with open(file_path, 'rb') as f:
            return plistlib.load(f)
    except Exception:
        # Silently fail - will try text format fallback
        return {}

def parse_json(file_path: str) -> dict:
    """Parse a JSON file from powermetrics"""
    try:
        with open(file_path, 'r') as f:
            return json.load(f)
    except Exception as e:
        print(f"Error parsing JSON {file_path}: {e}")
        return {}

def extract_cpu_power_samples(data: dict) -> List[float]:
    """Extract CPU power samples from powermetrics output"""
    samples = []

    # Try to find CPU power data (format varies by macOS version)
    if isinstance(data, dict):
        # Look for various possible keys in the plist
        for key in ['CPU Package Power', 'cpu_power', 'CPUPower', 'processor']:
            if key in data:
                value = data[key]
                if isinstance(value, (int, float)):
                    samples.append(float(value))

        # Check for nested structures
        if 'processor' in data and isinstance(data['processor'], dict):
            proc = data['processor']
            for key in ['power', 'cpu_power', 'package_power']:
                if key in proc:
                    value = proc[key]
                    if isinstance(value, (int, float)):
                        samples.append(float(value))

        # Look in any array-like structures
        for value in data.values():
            if isinstance(value, dict):
                for k, v in value.items():
                    if 'power' in k.lower() and isinstance(v, (int, float)):
                        samples.append(float(v))

    return samples

def extract_combined_power_samples(text_content: str) -> List[float]:
    """Extract combined power samples from text format powermetrics"""
    samples = []
    for line in text_content.split('\n'):
        if line.startswith('Combined Power'):
            # Extract number from "Combined Power (CPU + GPU + ANE): 1475 mW"
            parts = line.split(': ')
            if len(parts) == 2:
                power_str = parts[1].replace(' mW', '')
                try:
                    mW = float(power_str)
                    samples.append(mW / 1000)  # Convert to watts
                except:
                    pass
    return samples

def extract_gpu_power_samples(data: dict) -> List[float]:
    """Extract GPU power samples from powermetrics output"""
    samples = []

    if isinstance(data, dict):
        for key in ['GPU Power', 'gpu_power', 'GPUPower']:
            if key in data:
                value = data[key]
                if isinstance(value, (int, float)):
                    samples.append(float(value))

        # Check nested GPU structures
        if 'gpu' in data and isinstance(data['gpu'], dict):
            gpu = data['gpu']
            for key in ['power', 'gpu_power', 'package_power']:
                if key in gpu:
                    value = gpu[key]
                    if isinstance(value, (int, float)):
                        samples.append(float(value))

    return samples

def extract_idle_residency(data: dict) -> Dict[str, float]:
    """Extract CPU C-state residency data"""
    residency = {}

    if isinstance(data, dict):
        # Look for C-state data
        for key in data.keys():
            if 'c-state' in key.lower() or 'residency' in key.lower():
                value = data[key]
                if isinstance(value, (int, float)):
                    residency[key] = float(value)

    return residency

def analyze_power_data(cpu_samples: List[float], gpu_samples: List[float]) -> Dict[str, float]:
    """Analyze power samples and calculate statistics"""
    stats = {}

    if cpu_samples:
        stats['cpu_avg_power_W'] = statistics.mean(cpu_samples)
        stats['cpu_min_power_W'] = min(cpu_samples)
        stats['cpu_max_power_W'] = max(cpu_samples)
        stats['cpu_stdev_power_W'] = statistics.stdev(cpu_samples) if len(cpu_samples) > 1 else 0
    else:
        stats['cpu_avg_power_W'] = 0
        stats['cpu_min_power_W'] = 0
        stats['cpu_max_power_W'] = 0
        stats['cpu_stdev_power_W'] = 0

    if gpu_samples:
        stats['gpu_avg_power_W'] = statistics.mean(gpu_samples)
        stats['gpu_min_power_W'] = min(gpu_samples)
        stats['gpu_max_power_W'] = max(gpu_samples)
    else:
        stats['gpu_avg_power_W'] = 0
        stats['gpu_min_power_W'] = 0
        stats['gpu_max_power_W'] = 0

    return stats

def calculate_energy_per_frame(avg_power_W: float, duration_ms: float, frame_count: int = 3600) -> float:
    """
    Calculate estimated energy per frame

    Assumes 60 FPS * 60 seconds = 3600 frames for a 60-second test
    Energy (mJ) = Power (W) * Time (s) / Frame Count
    """
    if frame_count == 0:
        return 0
    duration_s = duration_ms / 1000.0
    energy_per_frame_J = (avg_power_W * duration_s) / frame_count
    return energy_per_frame_J * 1000  # Convert to mJ

def calculate_battery_life(avg_power_W: float, battery_capacity_Wh: float = 70) -> Dict[str, float]:
    """Calculate battery life estimate"""
    if avg_power_W <= 0:
        return {}

    hours = battery_capacity_Wh / avg_power_W
    return {
        'hours': hours,
        'minutes': hours * 60,
        'days': hours / 24
    }

def print_power_analysis(file_path: str, samples: List[float], battery_wh: float = 70):
    """Print power consumption analysis with battery life estimate"""
    if not samples:
        return

    avg = statistics.mean(samples)
    min_val = min(samples)
    max_val = max(samples)
    stdev = statistics.stdev(samples) if len(samples) > 1 else 0

    print(f"\n{'='*70}")
    print(f"Power Analysis: {Path(file_path).name}")
    print(f"{'='*70}")

    print(f"\nActual Power Consumption:")
    print(f"  Average:      {avg:.2f} W")
    print(f"  Min:          {min_val:.2f} W (during sleep/vsync)")
    print(f"  Max:          {max_val:.2f} W (peak gameplay)")
    print(f"  Std Dev:      {stdev:.2f} W")
    print(f"  Samples:      {len(samples)}")

    print(f"\nBattery Life Estimates (M3 Pro 14\": {battery_wh} Wh):")
    print(f"  Average Load:      {avg:.1f}W → {battery_wh/avg:.1f} hours ({(battery_wh/avg)*60:.0f} min)")
    print(f"  Idle/Sleep:        {min_val:.2f}W → {battery_wh/min_val:.0f} hours")
    print(f"  Peak Gameplay:     {max_val:.2f}W → {battery_wh/max_val:.1f} hours")

    # Estimate based on residency pattern (81% idle, 19% active)
    if avg > 0.2:  # Only estimate if not just idle
        estimated_active_power = avg / 0.19  # If 19% active contributes to this avg
        continuous_gameplay_hours = battery_wh / estimated_active_power

        print(f"\nLoad Breakdown Estimate:")
        print(f"  81% idle time:     ~{min_val:.2f}W")
        print(f"  19% active time:   ~{estimated_active_power:.2f}W")
        print(f"  → Continuous gameplay (no idle): {continuous_gameplay_hours:.1f} hours")

    print(f"\n{'='*70}\n")

def print_analysis(file_path: str, data: dict, stats: Dict[str, float]):
    """Print formatted analysis results"""
    print(f"\n{'='*70}")
    print(f"Analysis: {Path(file_path).name}")
    print(f"{'='*70}")

    print(f"\nCPU Power Statistics:")
    print(f"  Average:  {stats.get('cpu_avg_power_W', 0):.2f} W")
    print(f"  Min:      {stats.get('cpu_min_power_W', 0):.2f} W")
    print(f"  Max:      {stats.get('cpu_max_power_W', 0):.2f} W")
    print(f"  Std Dev:  {stats.get('cpu_stdev_power_W', 0):.2f} W")

    if stats.get('gpu_avg_power_W', 0) > 0:
        print(f"\nGPU Power Statistics:")
        print(f"  Average:  {stats.get('gpu_avg_power_W', 0):.2f} W")
        print(f"  Min:      {stats.get('gpu_min_power_W', 0):.2f} W")
        print(f"  Max:      {stats.get('gpu_max_power_W', 0):.2f} W")

    # Calculate energy per frame (60 FPS baseline)
    energy_per_frame = calculate_energy_per_frame(stats.get('cpu_avg_power_W', 0), 60000)
    print(f"\nEstimated Energy (60 FPS baseline):")
    print(f"  Per Frame: {energy_per_frame:.1f} mJ")
    print(f"  Per Second: {energy_per_frame * 60:.1f} mJ")

    print(f"\n{'='*70}\n")

def analyze_text_data(data: dict) -> Dict[str, float]:
    """Analyze text format powermetrics data and extract statistics"""
    stats = {}

    # Average active residency across all CPUs
    if data.get('active_residencies'):
        active_values = [pct for _, pct in data['active_residencies']]
        stats['avg_active_residency_pct'] = statistics.mean(active_values)
        stats['min_active_residency_pct'] = min(active_values)
        stats['max_active_residency_pct'] = max(active_values)
    else:
        stats['avg_active_residency_pct'] = 0
        stats['min_active_residency_pct'] = 0
        stats['max_active_residency_pct'] = 0

    # Average idle residency across all CPUs
    if data.get('idle_residencies'):
        idle_values = [pct for _, pct in data['idle_residencies']]
        stats['avg_idle_residency_pct'] = statistics.mean(idle_values)
        stats['min_idle_residency_pct'] = min(idle_values)
        stats['max_idle_residency_pct'] = max(idle_values)
    else:
        stats['avg_idle_residency_pct'] = 0
        stats['min_idle_residency_pct'] = 0
        stats['max_idle_residency_pct'] = 0

    # Average frequency across all CPUs
    if data.get('cpu_frequencies'):
        freq_values = [freq for _, freq in data['cpu_frequencies']]
        stats['avg_frequency_mhz'] = statistics.mean(freq_values)
        stats['min_frequency_mhz'] = min(freq_values)
        stats['max_frequency_mhz'] = max(freq_values)
    else:
        stats['avg_frequency_mhz'] = 0
        stats['min_frequency_mhz'] = 0
        stats['max_frequency_mhz'] = 0

    # CPU count
    stats['cpu_count'] = len(data.get('active_residencies', []))

    return stats

def print_text_analysis(file_path: str, data: dict, stats: Dict[str, float]):
    """Print formatted analysis for text format powermetrics data"""
    print(f"\n{'='*70}")
    print(f"Analysis: {Path(file_path).name}")
    print(f"{'='*70}")

    print(f"\nCPU Residency Statistics ({int(stats['cpu_count'])} CPUs):")
    print(f"  Active Residency:  {stats.get('avg_active_residency_pct', 0):>6.2f}% avg (min: {stats.get('min_active_residency_pct', 0):.2f}%, max: {stats.get('max_active_residency_pct', 0):.2f}%)")
    print(f"  Idle Residency:    {stats.get('avg_idle_residency_pct', 0):>6.2f}% avg (min: {stats.get('min_idle_residency_pct', 0):.2f}%, max: {stats.get('max_idle_residency_pct', 0):.2f}%)")

    print(f"\nCPU Frequency Statistics:")
    print(f"  Average:  {stats.get('avg_frequency_mhz', 0):.0f} MHz")
    print(f"  Min:      {stats.get('min_frequency_mhz', 0):.0f} MHz")
    print(f"  Max:      {stats.get('max_frequency_mhz', 0):.0f} MHz")

    # Per-CPU breakdown if available
    if data.get('active_residencies'):
        print(f"\nPer-CPU Breakdown:")
        for cpu_num, active_pct in data.get('active_residencies', []):
            idle_pct = next((pct for num, pct in data.get('idle_residencies', []) if num == cpu_num), 0)
            freq = next((f for num, f in data.get('cpu_frequencies', []) if num == cpu_num), 0)
            print(f"  CPU {cpu_num}: Active {active_pct:6.2f}%, Idle {idle_pct:6.2f}%, Freq {freq:7.0f} MHz")

    print(f"\n{'='*70}\n")

def parse_benchmark_log(file_path: str) -> dict:
    """Extract performance metrics from headless benchmark log"""
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


def find_benchmark_log(plist_file: str) -> str:
    """Find corresponding benchmark log for a plist file"""
    # Convert power_multi_50000_20251225_064755.plist -> benchmark_multi_50000_20251225_064755.txt
    plist_path = Path(plist_file)
    parts = plist_path.name.split('_')
    if parts[0] == 'power' and len(parts) >= 4:
        benchmark_name = 'benchmark_' + '_'.join(parts[1:]).replace('.plist', '.txt')
        benchmark_file = plist_path.parent / benchmark_name
        if benchmark_file.exists():
            return str(benchmark_file)
    return None


def print_headless_analysis(plist_file: str, bench_file: str, power_samples: List[float]):
    """Print headless benchmark analysis with performance metrics"""

    # Parse benchmark log
    bench_stats = parse_benchmark_log(bench_file)
    if not bench_stats:
        print(f"Warning: Could not parse benchmark log {bench_file}")
        return

    # Analyze power
    power_stats = analyze_power_data(power_samples, [])

    print(f"\n{'='*70}")
    print(f"Headless Benchmark Analysis: {Path(plist_file).name}")
    print(f"{'='*70}")

    print(f"\nPerformance Metrics:")
    print(f"  Entity Count:       {bench_stats.get('entity_count', 0):,}")
    print(f"  Threading Mode:     {bench_stats.get('threading_mode', '?')}")
    print(f"  Active Workers:     {bench_stats.get('workers_active', '?')}")
    print(f"  Total Frames:       {bench_stats.get('total_frames', 0):,}")
    print(f"  Avg Frame Time:     {bench_stats.get('avg_frame_time_ms', 0):.2f}ms")
    print(f"  Avg FPS:            {bench_stats.get('avg_fps', 0):.1f}")

    print(f"\nPower Consumption (AI/Collision/Pathfinding only - NO RENDERING):")
    print(f"  Average:            {power_stats.get('cpu_avg_power_W', 0):.2f}W")
    print(f"  Min:                {power_stats.get('cpu_min_power_W', 0):.2f}W")
    print(f"  Max:                {power_stats.get('cpu_max_power_W', 0):.2f}W")
    print(f"  Std Dev:            {power_stats.get('cpu_stdev_power_W', 0):.2f}W")

    # Calculate efficiency metrics
    entity_count = bench_stats.get('entity_count', 0)
    avg_fps = bench_stats.get('avg_fps', 1)
    avg_power = power_stats.get('cpu_avg_power_W', 1)
    total_time_ms = bench_stats.get('avg_frame_time_ms', 0) * bench_stats.get('total_frames', 1)

    print(f"\nEfficiency Metrics:")
    if entity_count > 0:
        print(f"  Power per entity:   {(avg_power * 1000) / entity_count:.3f}mW/entity")
        print(f"  Throughput:         {(entity_count * avg_fps):,.0f} entity-updates/sec")
    else:
        print(f"  Idle baseline (no entities)")
        print(f"  Throughput:         N/A")

    # Battery drain calculation (realistic)
    battery_wh = 70
    total_time_hours = total_time_ms / (1000 * 3600)
    battery_drain_wh = avg_power * total_time_hours
    battery_drain_pct = (battery_drain_wh / battery_wh) * 100

    print(f"\nBattery Impact (This Test):")
    print(f"  Test Duration:      {total_time_hours*60:.1f} minutes")
    print(f"  Energy Consumed:    {battery_drain_wh:.4f}Wh")
    print(f"  Battery Drain:      {battery_drain_pct:.3f}%")

    # Theoretical continuous play (unrealistic - no rendering)
    hours = battery_wh / avg_power if avg_power > 0 else 0
    print(f"\n⚠️  Theoretical Continuous Play (HEADLESS - NO RENDERING):")
    print(f"  {hours:.1f} hours at {avg_power:.2f}W")
    print(f"  ✗ This is NOT realistic - rendering adds ~15-20W")
    print(f"  ✓ For actual battery estimates, use real-app gameplay data")

    print(f"\n{'='*70}\n")


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='Parse powermetrics data for real-app or headless benchmarks'
    )
    parser.add_argument('files', nargs='+', help='Plist files to analyze')
    parser.add_argument('--real-app', action='store_true',
                       help='Parse real-app gameplay data (default behavior)')
    parser.add_argument('--headless', action='store_true',
                       help='Parse headless benchmark data (auto-finds benchmark logs)')

    args = parser.parse_args()

    # Determine mode
    auto_detect = not args.real_app and not args.headless

    # Filter out empty strings from argparse
    args.files = [f for f in args.files if f.strip()]

    if len(args.files) < 1:
        parser.print_help()
        sys.exit(1)

    print("PowerMetrics Parser - SDL3 HammerEngine Power Analysis")
    print("="*70)

    all_results = []

    for file_path in args.files:
        if not Path(file_path).exists():
            print(f"Warning: File not found: {file_path}")
            continue

        file_ext = Path(file_path).suffix.lower()

        # Try to detect file format
        data = {}
        is_text_format = False

        if file_ext == '.plist':
            # Try binary plist first, then fall back to text
            data = parse_plist(file_path)
            if not data:
                data = parse_text_format(file_path)
                is_text_format = True
        elif file_ext == '.json':
            data = parse_json(file_path)
        elif file_ext == '.txt':
            data = parse_text_format(file_path)
            is_text_format = True
        else:
            data = parse_text_format(file_path)
            is_text_format = bool(data)

        if not data:
            print(f"Warning: No data parsed from {file_path}")
            continue

        # Extract power samples from text format
        power_samples = []
        if is_text_format:
            with open(file_path, 'r') as f:
                text_content = f.read()
            power_samples = extract_combined_power_samples(text_content)

        # Determine if this is headless or real-app
        is_headless = False
        if auto_detect and is_text_format and 'power_' in Path(file_path).name:
            # Check if corresponding benchmark log exists
            bench_file = find_benchmark_log(file_path)
            is_headless = bench_file is not None

        # Process based on mode
        if args.headless or (auto_detect and is_headless):
            # Headless benchmark mode
            bench_file = find_benchmark_log(file_path)
            if bench_file and power_samples:
                all_results.append({
                    'file': file_path,
                    'power_samples': power_samples,
                    'format': 'headless'
                })
                print_headless_analysis(file_path, bench_file, power_samples)
            else:
                print(f"Warning: Could not find benchmark log for {file_path}")
        else:
            # Real-app mode
            if is_text_format:
                stats = analyze_text_data(data)
                all_results.append({
                    'file': file_path,
                    'stats': stats,
                    'format': 'text',
                    'data': data
                })
                print_text_analysis(file_path, data, stats)

                if power_samples:
                    all_results[-1]['power_samples'] = power_samples
                    print_power_analysis(file_path, power_samples)
            else:
                # Binary/JSON format
                cpu_samples = extract_cpu_power_samples(data)
                gpu_samples = extract_gpu_power_samples(data)

                stats = analyze_power_data(cpu_samples, gpu_samples)
                all_results.append({
                    'file': file_path,
                    'stats': stats,
                    'format': 'binary',
                    'cpu_samples': cpu_samples,
                    'gpu_samples': gpu_samples
                })
                print_analysis(file_path, data, stats)

    # Summary comparison
    if len(all_results) > 1:
        print(f"\n{'='*70}")
        print("Comparison Summary")
        print(f"{'='*70}\n")

        # Check if we have text format results (C-state residency data)
        text_results = [r for r in all_results if r.get('format') == 'text']
        binary_results = [r for r in all_results if r.get('format') == 'binary']

        if text_results:
            # Text format comparison (C-state residency)
            print(f"{'File':<40} {'Idle Residency':<20} {'Active %':<15}")
            print(f"{'-'*75}")
            for result in text_results:
                idle = result['stats'].get('avg_idle_residency_pct', 0)
                active = result['stats'].get('avg_active_residency_pct', 0)
                file_name = Path(result['file']).name
                print(f"{file_name:<40} {idle:>16.2f}%  {active:>12.2f}%")

            # Calculate idle time improvement if we have multiple text results
            if len(text_results) >= 2:
                baseline_idle = text_results[0]['stats'].get('avg_idle_residency_pct', 0)
                optimized_idle = text_results[-1]['stats'].get('avg_idle_residency_pct', 0)

                if baseline_idle > 0:
                    idle_improvement = optimized_idle - baseline_idle
                    print(f"\nIdle Residency Improvement: {idle_improvement:+.2f}% (higher is better for race-to-idle)")

                    if optimized_idle > baseline_idle:
                        print("✓ Race-to-idle strategy is WORKING - more time in C-states (deep sleep)")
                    else:
                        print("✗ Race-to-idle not showing expected improvement")

        if binary_results:
            # Binary format comparison (power draw)
            print(f"{'File':<40} {'Avg CPU Power':<15} {'Avg GPU Power':<15}")
            print(f"{'-'*70}")
            for result in binary_results:
                cpu_power = result['stats'].get('cpu_avg_power_W', 0)
                gpu_power = result['stats'].get('gpu_avg_power_W', 0)
                file_name = Path(result['file']).name
                print(f"{file_name:<40} {cpu_power:>12.2f} W  {gpu_power:>12.2f} W")

            # Calculate efficiency metric
            if len(binary_results) >= 2:
                baseline_power = binary_results[0]['stats'].get('cpu_avg_power_W', 1)
                optimized_power = binary_results[-1]['stats'].get('cpu_avg_power_W', baseline_power)

                if baseline_power > 0:
                    power_reduction = ((baseline_power - optimized_power) / baseline_power) * 100
                    print(f"\nPower Reduction: {power_reduction:+.1f}%")

        print(f"\n{'='*70}\n")

if __name__ == '__main__':
    main()
