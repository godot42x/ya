#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 反射代码生成器

功能：
    - 使用 libclang 解析 C++ 头文件
    - 提取类、方法、属性的反射信息
    - 生成运行时反射注册代码

使用方法：
    python main.py input.h -o output.generated.h
    python main.py input1.h input2.h -o output.generated.h
    python main.py input.h -I include_dir -D DEFINE_MACRO

依赖：
    pip install libclang
"""

import os
import sys
import argparse
from typing import List


# ============================================================================
# 依赖检查
# ============================================================================
def check_and_install_dependencies():
    """检查并安装必要的 Python 模块"""
    required_modules = ["clang"]
    missing_modules = []

    for module in required_modules:
        try:
            __import__(module)
        except ImportError:
            missing_modules.append(module)

    if missing_modules:
        print(f"[WARNING] Missing dependencies: {', '.join(missing_modules)}")
        print("📦 正在安装...")
        for module in missing_modules:
            os.system(f"python -m pip install {module}")
        print("[SUCCESS] Dependencies installed\n")


# 检查依赖
check_and_install_dependencies()

# 导入模块
try:
    import clang.cindex
    from cpp_parser import CppParser
    from code_generator import CodeGenerator
except ImportError as e:
    print(f"[ERROR] Import failed: {e}")
    print("请确保所有模块文件在同一目录下")
    sys.exit(1)


# ============================================================================
# 主函数
# ============================================================================
def main() -> int:
    """
    主函数：解析命令行参数并执行代码生成

    返回:
        0 表示成功, 1 表示失败
    """
    # 解析命令行参数
    parser = argparse.ArgumentParser(
        description="C++ 反射代码生成器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python main.py input.h -o output.generated.h
  python main.py input1.h input2.h -o output.generated.h
  python main.py input.h -I./include -DDEBUG
  python main.py input.h --report  # 显示反射信息报告
        """,
    )

    parser.add_argument("input", nargs="+", help="输入的 C++ 头文件")
    parser.add_argument(
        "-o",
        "--output",
        default="reflection.generated.h",
        help="输出的生成文件（默认: reflection.generated.h）",
    )
    parser.add_argument("-I", "--include", action="append", default=[], help="包含目录")
    parser.add_argument(
        "-D", "--define", action="append", default=[], help="预处理器定义"
    )
    parser.add_argument("--report", action="store_true", help="打印反射信息报告")
    parser.add_argument("--libclang", help="libclang 库路径（可选）")

    args = parser.parse_args()

    # 设置 libclang 路径（如果提供）
    if args.libclang:
        clang.cindex.Config.set_library_file(args.libclang)

    # ========================================================================
    # 步骤 1: 解析所有输入文件
    # ========================================================================
    print("=" * 70)
    print("[Generator] C++ Reflection Code Generator")
    print("=" * 70)
    print()

    parser_obj = CppParser(include_paths=args.include, defines=args.define)

    success = True
    for input_file in args.input:
        if not parser_obj.parse_file(input_file):
            success = False
            print()

    if not success:
        print("[ERROR] Parsing failed!")
        return 1

    # ========================================================================
    # 步骤 2: 生成反射代码
    # ========================================================================
    print()
    print(f"[Output] Generating file: {args.output}")

    generator = CodeGenerator(parser_obj.reflection_info)
    generator.generate_header(args.output, args.input)

    # ========================================================================
    # 步骤 3: 显示报告（如果请求）
    # ========================================================================
    if args.report:
        print()
        print(generator.generate_report())

    # ========================================================================
    # 步骤 4: 显示总结
    # ========================================================================
    num_classes = len(parser_obj.reflection_info.classes)
    num_enums = len(parser_obj.reflection_info.enums)

    print()
    print("=" * 70)
    print("[SUCCESS] Code generation completed")
    print(
        f"   Generated reflection code for {num_classes} classes and {num_enums} enums"
    )
    print("=" * 70)

    return 0


# ============================================================================
# 入口点
# ============================================================================
if __name__ == "__main__":
    sys.exit(main())
