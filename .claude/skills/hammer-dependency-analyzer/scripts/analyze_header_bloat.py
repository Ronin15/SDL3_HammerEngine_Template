#!/usr/bin/env python3
"""
Analyze header bloat and forward declaration opportunities in HammerEngine
"""

import os
import re
from collections import defaultdict

def find_headers(base_dir):
    """Find all header files"""
    headers = []
    for root, dirs, files in os.walk(os.path.join(base_dir, 'include')):
        for file in files:
            if file.endswith('.hpp'):
                headers.append(os.path.join(root, file))
    return sorted(headers)

def count_includes(file_path):
    """Count number of includes in a file"""
    count = 0
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                if re.match(r'^\s*#include', line):
                    count += 1
    except Exception:
        pass
    return count

def extract_includes(file_path):
    """Extract local includes from a file"""
    includes = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                match = re.match(r'^\s*#include\s+"([^"]+)"', line)
                if match:
                    includes.append(match.group(1))
    except Exception:
        pass
    return includes

def count_usages_in_file(file_path, class_name):
    """Count how many times a class is used in a file"""
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        # Look for pointer/reference usage: ClassName* or ClassName&
        ptr_ref_pattern = rf'\b{class_name}\s*[*&]'
        ptr_ref_count = len(re.findall(ptr_ref_pattern, content))

        # Look for direct usage: ClassName variable; or ClassName::
        direct_pattern = rf'\b{class_name}\s+\w+[;\(]|\b{class_name}::'
        direct_count = len(re.findall(direct_pattern, content))

        # Look for member variables: m_className
        member_pattern = rf'\bm_\w*{class_name}\w*'
        member_count = len(re.findall(member_pattern, content, re.IGNORECASE))

        return {
            'ptr_ref': ptr_ref_count,
            'direct': direct_count,
            'member': member_count
        }
    except Exception:
        return {'ptr_ref': 0, 'direct': 0, 'member': 0}

def analyze_header_bloat(base_dir):
    """Analyze header bloat"""
    headers = find_headers(base_dir)

    print("=== Header Bloat Analysis ===")
    print()

    # Find headers with high include counts
    include_counts = []
    for header in headers:
        count = count_includes(header)
        include_counts.append((os.path.basename(header), count))

    include_counts.sort(key=lambda x: x[1], reverse=True)

    print("Headers with High Include Count (potential bloat):")
    print()

    high_bloat = []
    for name, count in include_counts:
        if count > 15:
            print(f"  🔴 {name:45s} {count} includes (HIGH - review for bloat)")
            high_bloat.append(name)
        elif count > 10:
            print(f"  ⚠️  {name:45s} {count} includes (MODERATE)")
            high_bloat.append(name)
        elif count > 7:
            print(f"  🟡 {name:45s} {count} includes")

    print()

    return high_bloat

def analyze_frequently_included(base_dir):
    """Find frequently included headers"""
    headers = find_headers(base_dir)

    # Count how many files include each header
    include_freq = defaultdict(int)

    for header in headers:
        includes = extract_includes(header)
        for include in includes:
            include_name = os.path.basename(include)
            include_freq[include_name] += 1

    print("Frequently Included Headers (ripple effect on compile times):")
    print()

    sorted_freq = sorted(include_freq.items(), key=lambda x: x[1], reverse=True)

    ripple_headers = []
    for header_name, freq in sorted_freq[:15]:
        # Find the actual header file
        header_path = None
        for h in headers:
            if os.path.basename(h) == header_name:
                header_path = h
                break

        if header_path:
            header_includes = count_includes(header_path)

            status = ""
            if header_includes > 10:
                status = f"⚠️  bloat amplification ({header_includes} includes)"
                ripple_headers.append((header_name, freq, header_includes))

            print(f"  {header_name:45s} included by {freq:2d} files {status}")

    print()

    return ripple_headers

def analyze_forward_declaration_opportunities(base_dir):
    """Find forward declaration opportunities"""
    headers = find_headers(base_dir)

    print("=== Forward Declaration Opportunities ===")
    print()

    opportunities = []

    for header in headers:
        includes = extract_includes(header)

        for include in includes:
            include_name = os.path.basename(include)
            class_name = include_name.replace('.hpp', '')

            # Analyze usage in the header
            usage = count_usages_in_file(header, class_name)

            # If only used as pointer/reference and not directly, it's a candidate
            if usage['ptr_ref'] > 0 and usage['direct'] == 0:
                opportunities.append({
                    'header': os.path.basename(header),
                    'include': include_name,
                    'class': class_name
                })

    # Show top opportunities
    print(f"Found {len(opportunities)} forward declaration opportunities:")
    print()

    for i, opp in enumerate(opportunities[:20], 1):
        print(f"  {i:2d}. {opp['header']:40s}")
        print(f"      Can forward-declare {opp['class']}")
        print(f"      Remove: #include \"{opp['include']}\"")
        print(f"      Add: class {opp['class']};  // Forward declaration")
        print()

    if len(opportunities) > 20:
        print(f"  ... and {len(opportunities) - 20} more opportunities")
        print()

    print("Note: Move #include to .cpp file after forward declaration")
    print()

    return opportunities

def main():
    base_dir = '.'

    high_bloat = analyze_header_bloat(base_dir)
    print()

    ripple_headers = analyze_frequently_included(base_dir)
    print()

    opportunities = analyze_forward_declaration_opportunities(base_dir)

    # Estimate compile time savings
    if opportunities:
        estimated_savings = len(opportunities) * 0.5  # Rough estimate
        print(f"Estimated Compile Time Savings: ~{estimated_savings:.0f}% reduction")
        print()

    # Save results
    output_file = 'test_results/dependency_analysis/header_bloat_analysis.txt'
    with open(output_file, 'w') as f:
        f.write("Header Bloat Analysis\n")
        f.write("=" * 60 + "\n\n")

        f.write("High-Bloat Headers:\n")
        for header in high_bloat:
            f.write(f"  - {header}\n")
        f.write("\n")

        f.write("Ripple Effect Headers:\n")
        for header, freq, includes in ripple_headers:
            f.write(f"  - {header} (included by {freq} files, has {includes} includes)\n")
        f.write("\n")

        f.write(f"Forward Declaration Opportunities: {len(opportunities)}\n")

    print(f"Header bloat analysis saved to: {output_file}")

if __name__ == '__main__':
    main()
