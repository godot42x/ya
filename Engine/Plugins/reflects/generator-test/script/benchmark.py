#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Performance Benchmark for C++ Reflection Generator

This script benchmarks the reflection code generator's performance with various
file sizes and complexity levels.
"""

import sys
import os
import time
import subprocess
import tempfile
import statistics
from pathlib import Path
from typing import List, Dict, Tuple

# Configuration
SCRIPT_DIR = Path(__file__).parent.absolute()
GENERATOR_SCRIPT = SCRIPT_DIR.parent.parent / "generator" / "main.py"
BENCHMARK_RUNS = 5  # Number of runs per test case

class BenchmarkResult:
    """Stores benchmark results for a single test case"""
    def __init__(self, name: str, file_size_kb: float, lines: int):
        self.name = name
        self.file_size_kb = file_size_kb
        self.lines = lines
        self.times: List[float] = []
        
    def add_time(self, elapsed: float):
        self.times.append(elapsed)
        
    def get_stats(self) -> Dict[str, float]:
        if not self.times:
            return {}
        return {
            'min': min(self.times),
            'max': max(self.times),
            'mean': statistics.mean(self.times),
            'median': statistics.median(self.times),
            'stdev': statistics.stdev(self.times) if len(self.times) > 1 else 0.0
        }


def generate_test_file(num_classes: int, properties_per_class: int, 
                       methods_per_class: int) -> Tuple[str, int, float]:
    """
    Generate a test header file with specified complexity
    
    Returns: (content, line_count, size_in_kb)
    """
    lines = []
    lines.append('#pragma once')
    lines.append('#include <string>')
    lines.append('#include <vector>')
    lines.append('')
    
    for class_idx in range(num_classes):
        class_name = f"TestClass{class_idx}"
        lines.append(f"class [[refl::uclass]] {class_name} {{")
        lines.append("public:")
        
        # Generate properties
        for prop_idx in range(properties_per_class):
            prop_name = f"m_property{prop_idx}"
            prop_type = ['int', 'float', 'std::string', 'bool'][prop_idx % 4]
            lines.append(f"    [[refl::property]]")
            lines.append(f"    {prop_type} {prop_name};")
            lines.append('')
        
        # Generate methods
        for method_idx in range(methods_per_class):
            method_name = f"Method{method_idx}"
            return_type = ['void', 'int', 'float', 'std::string'][method_idx % 4]
            param = f"int param{method_idx}" if method_idx % 2 == 0 else ""
            lines.append(f"    [[refl::property]] {return_type} {method_name}({param}) {{ }}")
            lines.append('')
        
        lines.append("};")
        lines.append('')
    
    content = '\n'.join(lines)
    line_count = len(lines)
    size_kb = len(content.encode('utf-8')) / 1024.0
    
    return content, line_count, size_kb


def run_generator(input_file: Path, output_file: Path) -> float:
    """
    Run the generator and return elapsed time in seconds
    """
    cmd = [
        sys.executable,
        str(GENERATOR_SCRIPT),
        str(input_file),
        "-o", str(output_file)
    ]
    
    start_time = time.perf_counter()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    elapsed = time.perf_counter() - start_time
    
    if result.returncode != 0:
        error_msg = result.stderr if result.stderr else result.stdout
        raise RuntimeError(f"Generator failed (exit {result.returncode}): {error_msg}")
    
    return elapsed


def run_benchmark_case(name: str, num_classes: int, properties_per_class: int,
                       methods_per_class: int) -> BenchmarkResult:
    """
    Run benchmark for a specific test case
    """
    print(f"\n[Benchmark] {name}")
    print(f"  Classes: {num_classes}, Properties/class: {properties_per_class}, Methods/class: {methods_per_class}")
    
    # Generate test file
    content, lines, size_kb = generate_test_file(num_classes, properties_per_class, methods_per_class)
    result = BenchmarkResult(name, size_kb, lines)
    
    print(f"  Generated file: {lines} lines, {size_kb:.2f} KB")
    
    # Create temp files
    with tempfile.TemporaryDirectory() as tmpdir:
        input_file = Path(tmpdir) / "test_input.h"
        output_file = Path(tmpdir) / "test_output.generated.h"
        
        input_file.write_text(content, encoding='utf-8')
        
        # Run multiple times
        print(f"  Running {BENCHMARK_RUNS} iterations...", end='', flush=True)
        for i in range(BENCHMARK_RUNS):
            try:
                elapsed = run_generator(input_file, output_file)
                result.add_time(elapsed)
                print('.', end='', flush=True)
            except Exception as e:
                print(f"\n  [ERROR] Run {i+1} failed: {e}")
                return result
        
        print(" Done!")
    
    return result


def print_results(results: List[BenchmarkResult]):
    """
    Print formatted benchmark results
    """
    print("\n" + "="*80)
    print("PERFORMANCE BENCHMARK RESULTS")
    print("="*80)
    
    # Header
    print(f"\n{'Test Case':<30} {'Lines':<8} {'Size(KB)':<10} {'Min(s)':<10} {'Mean(s)':<10} {'Max(s)':<10} {'StdDev':<10}")
    print("-"*80)
    
    # Data rows
    for result in results:
        stats = result.get_stats()
        if not stats:
            print(f"{result.name:<30} {'N/A':<8} {result.file_size_kb:<10.2f} {'FAILED':<50}")
            continue
            
        print(f"{result.name:<30} {result.lines:<8} {result.file_size_kb:<10.2f} "
              f"{stats['min']:<10.4f} {stats['mean']:<10.4f} {stats['max']:<10.4f} {stats['stdev']:<10.4f}")
    
    print("\n" + "="*80)
    print("SUMMARY")
    print("="*80)
    
    total_runs = sum(len(r.times) for r in results)
    successful_runs = sum(1 for r in results if r.times)
    
    print(f"Total test cases: {len(results)}")
    print(f"Successful cases: {successful_runs}/{len(results)}")
    print(f"Total runs: {total_runs}")
    print(f"Runs per case: {BENCHMARK_RUNS}")
    
    # Calculate overall statistics
    all_times = []
    for result in results:
        all_times.extend(result.times)
    
    if all_times:
        print(f"\nOverall statistics:")
        print(f"  Fastest run: {min(all_times):.4f}s")
        print(f"  Slowest run: {max(all_times):.4f}s")
        print(f"  Average time: {statistics.mean(all_times):.4f}s")
        print(f"  Median time: {statistics.median(all_times):.4f}s")
    
    print("="*80)


def main():
    """Main benchmark runner"""
    print("="*80)
    print("[Performance Benchmark] C++ Reflection Generator")
    print("="*80)
    print(f"Generator script: {GENERATOR_SCRIPT}")
    print(f"Benchmark runs per case: {BENCHMARK_RUNS}")
    
    if not GENERATOR_SCRIPT.exists():
        print(f"\n[ERROR] Generator script not found: {GENERATOR_SCRIPT}")
        return 1
    
    # Define test cases: (name, num_classes, properties_per_class, methods_per_class)
    test_cases = [
        ("Small - 1 class", 1, 5, 3),
        ("Medium - 5 classes", 5, 10, 5),
        ("Large - 10 classes", 10, 15, 8),
        ("XLarge - 20 classes", 20, 20, 10),
        ("Complex - 50 classes", 50, 25, 12),
    ]
    
    results = []
    
    start_total = time.perf_counter()
    
    for name, num_classes, props, methods in test_cases:
        try:
            result = run_benchmark_case(name, num_classes, props, methods)
            results.append(result)
        except KeyboardInterrupt:
            print("\n\n[WARNING] Benchmark interrupted by user")
            break
        except Exception as e:
            print(f"\n[ERROR] Benchmark case failed: {e}")
            import traceback
            traceback.print_exc()
    
    total_elapsed = time.perf_counter() - start_total
    
    # Print results
    print_results(results)
    print(f"\nTotal benchmark time: {total_elapsed:.2f}s")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
