#!/usr/bin/env python3
"""
Example usage of the C++ Reflection Code Generator
"""

import subprocess
import sys
import os

def run_reflector():
    """Run the reflector on the example file"""
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    main_script = os.path.join(script_dir, "main.py")
    test_file = os.path.join(script_dir, "test_example.h")
    output_file = os.path.join(script_dir, "reflection.generated.h")
    
    print("=" * 60)
    print("Running C++ Reflection Code Generator")
    print("=" * 60)
    print(f"Input: {test_file}")
    print(f"Output: {output_file}")
    print()
    
    # Run the reflector
    cmd = [
        sys.executable,
        main_script,
        test_file,
        "-o", output_file,
        "--report"
    ]
    
    print(f"Command: {' '.join(cmd)}")
    print()
    
    result = subprocess.run(cmd)
    
    if result.returncode == 0:
        print("\n" + "=" * 60)
        print("SUCCESS!")
        print("=" * 60)
        print(f"\nGenerated file: {output_file}")
        
        # Show first few lines of generated file
        if os.path.exists(output_file):
            print("\nFirst 30 lines of generated file:")
            print("-" * 60)
            with open(output_file, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                for i, line in enumerate(lines[:30], 1):
                    print(f"{i:3d}: {line}", end='')
            if len(lines) > 30:
                print(f"\n... ({len(lines) - 30} more lines)")
            print("-" * 60)
    else:
        print("\n" + "=" * 60)
        print("FAILED!")
        print("=" * 60)
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(run_reflector())
