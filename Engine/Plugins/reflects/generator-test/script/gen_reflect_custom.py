#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
反射代码批量生成脚本

功能：
    1. 扫描源代码目录中所有带有 [[refl::uclass]] 标记的头文件
    2. 调用反射代码生成器为每个文件生成 .generated.h
    3. 支持自定义输出目录

使用方法：
    python gen_reflect_custom.py [选项]

示例：
    python gen_reflect_custom.py
    python gen_reflect_custom.py --source-dir ../src --output-dir ../intermediate/generates
"""

import os
import sys
import subprocess
import argparse
import glob
from pathlib import Path
from typing import List, Tuple


# ============================================================================
# 常量定义
# ============================================================================
REFLECTION_MARKER = "[[refl::uclass]]"
HEADER_EXTENSIONS = ["*.h", "*.hpp"]


# ============================================================================
# 文件扫描函数
# ============================================================================
def find_files_with_uclass(source_dir: str) -> List[str]:
    """
    扫描目录中所有包含反射标记的头文件
    
    参数：
        source_dir: 源代码目录路径
        
    返回：
        包含反射标记的头文件路径列表
    """
    print(f"[Scan] Scanning directory: {source_dir}")
    print("-" * 70)

    # 收集所有头文件
    header_files = []
    for pattern in HEADER_EXTENSIONS:
        header_files.extend(glob.glob(os.path.join(source_dir, pattern)))

    if not header_files:
        print("[WARNING] No header files found")
        return []

    # 检查每个头文件是否包含反射标记
    uclass_files = []
    for header_file in header_files:
        try:
            with open(header_file, "r", encoding="utf-8") as f:
                content = f.read()
                if REFLECTION_MARKER in content:
                    uclass_files.append(header_file)
                    filename = os.path.basename(header_file)
                    print(f"  [Found] Reflection class: {filename}")
        except Exception as e:
            filename = os.path.basename(header_file)
            print(f"  [ERROR] Failed to read: {filename} - {e}")

    print()
    return uclass_files


# ============================================================================
# 反射代码生成函数
# ============================================================================
def generate_reflection_code(
    input_file: str, 
    output_dir: str, 
    generator_script: str
) -> bool:
    """
    为单个头文件生成反射代码
    
    参数：
        input_file: 输入头文件路径
        output_dir: 输出目录路径
        generator_script: 生成器脚本路径
        
    返回：
        成功返回 True，失败返回 False
    """
    # 构建输出文件路径: common.h -> common.generated.h
    input_basename = os.path.basename(input_file)
    input_name = os.path.splitext(input_basename)[0]
    output_file = os.path.join(output_dir, f"{input_name}.generated.h")

    print(f"[Generate] Generating reflection code:")
    print(f"   输入: {input_basename}")
    print(f"   输出: {os.path.relpath(output_file)}")

    # 调用 Python 生成器
    cmd = [sys.executable, generator_script, input_file, "-o", output_file]

    try:
        result = subprocess.run(
            cmd, 
            capture_output=True, 
            text=True, 
            timeout=30  # 30秒超时
        )

        if result.returncode == 0:
            print(f"   [SUCCESS] Generated successfully")
            return True
        else:
            print(f"   [ERROR] Generation failed (exit code: {result.returncode})")
            if result.stderr:
                print(f"   错误信息: {result.stderr.strip()}")
            if result.stdout:
                print(f"   输出信息: {result.stdout.strip()}")
            return False
            
    except subprocess.TimeoutExpired:
        print(f"   [ERROR] Generation timeout (exceeded 30s)")
        return False
    except Exception as e:
        print(f"   [ERROR] Execution failed: {e}")
        return False


# ============================================================================
# 主函数
# ============================================================================
def main() -> int:
    """
    主函数：解析命令行参数并执行批量生成
    
    返回：
        0 表示成功，1 表示失败
    """
    # 解析命令行参数
    parser = argparse.ArgumentParser(
        description="C++ 反射代码批量生成工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例：
  python gen_reflect_custom.py
  python gen_reflect_custom.py --source-dir ../src
  python gen_reflect_custom.py --output-dir ../build/generated
        """
    )
    
    parser.add_argument(
        "--source-dir",
        default="../src",
        help="源代码目录（默认: ../src）"
    )
    parser.add_argument(
        "--output-dir",
        default="../intermediate/generates",
        help="生成代码输出目录（默认: ../intermediate/generates）"
    )
    parser.add_argument(
        "--generator",
        default="../../generator/main.py",
        help="生成器脚本路径（默认: ../../generator/main.py）"
    )

    args = parser.parse_args()

    # ========================================================================
    # 步骤 1: 准备路径
    # ========================================================================
    script_dir = os.path.dirname(os.path.abspath(__file__))
    source_dir = os.path.abspath(os.path.join(script_dir, args.source_dir))
    output_dir = os.path.abspath(os.path.join(script_dir, args.output_dir))
    generator_script = os.path.abspath(os.path.join(script_dir, args.generator))

    # 打印标题
    print("=" * 70)
    print("[Batch Generator] C++ Reflection Code Generator")
    print("=" * 70)
    print(f"[Input] Source directory:     {source_dir}")
    print(f"[Output] Output directory:   {output_dir}")
    print(f"[Script] Generator:     {generator_script}")
    print()

    # 验证路径
    if not os.path.exists(source_dir):
        print(f"[ERROR] Source directory does not exist: {source_dir}")
        return 1

    if not os.path.exists(generator_script):
        print(f"[ERROR] Generator script does not exist: {generator_script}")
        return 1

    # 确保输出目录存在
    os.makedirs(output_dir, exist_ok=True)

    # ========================================================================
    # 步骤 2: 扫描需要生成反射代码的文件
    # ========================================================================
    uclass_files = find_files_with_uclass(source_dir)

    if not uclass_files:
        print("[INFO] No header files containing [[refl::uclass]] marker found")
        return 0

    print(f"[Summary] Found {len(uclass_files)} header files that need reflection code generation")
    print()

    # ========================================================================
    # 步骤 3: 为每个文件生成反射代码
    # ========================================================================
    print("[Start] Generating reflection code:")
    print("-" * 70)
    
    success_count = 0
    fail_count = 0

    for i, uclass_file in enumerate(uclass_files, 1):
        print(f"[{i}/{len(uclass_files)}]")
        if generate_reflection_code(uclass_file, output_dir, generator_script):
            success_count += 1
        else:
            fail_count += 1
        print()

    # ========================================================================
    # 步骤 4: 显示总结
    # ========================================================================
    print("=" * 70)
    print("[Complete] Generation completed")
    print("=" * 70)
    print(f"  [SUCCESS] Succeeded: {success_count} files")
    print(f"  [ERROR] Failed: {fail_count} files")
    print()

    if fail_count > 0:
        print("[WARNING] Some files failed to generate, please check error messages above")
        return 1

    print("[SUCCESS] All files generated successfully!")
    print()
    print("[Next Steps]:")

    print("   1. 在源文件中通过 #if __has_include() 包含生成的 .generated.h")
    print("   2. 编译项目: xmake build reflects-generator-test")
    print("   3. 运行测试: xmake run reflects-generator-test")
    print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
