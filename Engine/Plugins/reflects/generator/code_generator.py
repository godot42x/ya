#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
反射代码生成器

根据反射信息生成 C++ 反射注册代码
"""

from pathlib import Path
from typing import List
from reflection_info import ReflectionInfo, ClassInfo, EnumInfo, MethodInfo


# ============================================================================
# 代码生成器
# ============================================================================
class CodeGenerator:
    """生成 C++ 反射注册代码"""
    
    def __init__(self, reflection_info: ReflectionInfo):
        self.reflection_info = reflection_info
    
    # ========================================================================
    # 主生成函数
    # ========================================================================
    def generate_header(self, output_file: str, input_files: List[str] = None):
        """
        生成反射注册头文件
        
        参数:
            output_file: 输出文件路径
            input_files: 输入文件列表（用于生成 include）
        """
        with open(output_file, 'w', encoding='utf-8') as f:
            self._write_header(f)
            self._write_includes(f, input_files)
            self._write_registration_namespace(f)
    
    def _write_header(self, f):
        """写入文件头"""
        f.write("// " + "="*70 + "\n")
        f.write("// 自动生成的反射注册代码\n")
        f.write("// 警告: 请勿手动编辑此文件！\n")
        f.write("// " + "="*70 + "\n\n")
        f.write("#pragma once\n\n")
    
    def _write_includes(self, f, input_files: List[str] = None):
        """写入包含指令"""
        # 反射框架
        f.write("// 反射框架\n")
        f.write("#include \"reflects-core/lib.h\"\n\n")
        
        # 被反射的类型
        if input_files:
            f.write("// 被反射的类型\n")
            for input_file in input_files:
                header_name = Path(input_file).name
                f.write(f"#include \"{header_name}\"\n")
            f.write("\n")
    
    def _write_registration_namespace(self, f):
        """写入注册代码（匿名命名空间）"""
        f.write("// " + "="*70 + "\n")
        f.write("// 反射注册代码\n")
        f.write("// " + "="*70 + "\n\n")
        f.write("namespace {\n\n")
        
        # 为每个类生成注册结构
        for class_info in self.reflection_info.classes:
            if self._should_generate(class_info):
                self._generate_class_registration(f, class_info)
        
        # 静态初始化
        f.write("// 静态初始化 - 程序启动时自动注册\n")
        for class_info in self.reflection_info.classes:
            if self._should_generate(class_info):
                safe_name = class_info.name.replace("::", "_")
                f.write(f"static {safe_name}Reflection g_{safe_name}Reflection;\n")
        
        f.write("\n} // namespace\n")
    
    # ========================================================================
    # 类注册生成
    # ========================================================================
    def _generate_class_registration(self, f, class_info: ClassInfo):
        """生成单个类的注册代码"""
        safe_name = class_info.name.replace("::", "_")
        
        f.write(f"// 注册类: {class_info.qualified_name}\n")
        f.write(f"struct {safe_name}Reflection {{\n")
        f.write(f"    {safe_name}Reflection() {{\n")
        f.write(f"        Register<{class_info.qualified_name}>(\"{class_info.name}\")\n")
        
        # 注册属性
        self._generate_properties(f, class_info)
        
        # 注册方法
        self._generate_methods(f, class_info)
        
        # 注册构造函数
        self._generate_constructors(f, class_info)
        
        f.write("            ;\n")
        f.write("    }\n")
        f.write("};\n\n")
    
    def _generate_properties(self, f, class_info: ClassInfo):
        """生成属性注册代码"""
        for field in class_info.fields:
            if field.access != "public":
                continue
                
            # 检查是否有 refl::property 标记
            has_refl_attr = any("refl::property" in attr for attr in field.annotations.keys())
            if has_refl_attr or not field.annotations:
                f.write(f"            .property(\"{field.name}\", "
                       f"&{class_info.qualified_name}::{field.name})\n")
    
    def _generate_methods(self, f, class_info: ClassInfo):
        """生成方法注册代码"""
        for method in class_info.methods:
            if method.access != "public":
                continue
            
            # 跳过析构函数和运算符
            if method.name.startswith("~") or method.name.startswith("operator"):
                continue
            
            # 检查是否有反射标记
            has_refl_attr = any("refl::" in attr for attr in method.annotations.keys())
            if has_refl_attr or True:  # 目前注册所有公有方法
                f.write(f"            .function(\"{method.name}\", "
                       f"&{class_info.qualified_name}::{method.name})\n")
    
    def _generate_constructors(self, f, class_info: ClassInfo):
        """生成构造函数注册代码"""
        if not class_info.constructors:
            # 没有找到构造函数，注册默认构造函数
            f.write("            .constructor()\n")
            return
        
        for ctor in class_info.constructors:
            if ctor.access != "public":
                continue
                
            if len(ctor.parameters) == 0:
                # 默认构造函数
                f.write("            .constructor()\n")
            else:
                # 带参数的构造函数
                param_types = ", ".join([
                    self._clean_type(ptype) for ptype, _ in ctor.parameters
                ])
                f.write(f"            .constructor<{param_types}>()\n")
    
    # ========================================================================
    # 报告生成
    # ========================================================================
    def generate_report(self) -> str:
        """生成可读性报告"""
        lines = [
            "="*70,
            "反射信息报告",
            "="*70,
            ""
        ]
        
        lines.append(f"📊 类数量: {len(self.reflection_info.classes)}")
        for class_info in self.reflection_info.classes:
            lines.append(f"\n  🔷 {class_info.qualified_name}")
            lines.append(f"     字段: {len(class_info.fields)}")
            for field in class_info.fields:
                lines.append(f"       - {field.access} {field.type_name} {field.name}")
            lines.append(f"     方法: {len(class_info.methods)}")
            for method in class_info.methods:
                params = ", ".join([ptype for ptype, _ in method.parameters])
                lines.append(f"       - {method.access} {method.return_type} {method.name}({params})")
            lines.append(f"     构造函数: {len(class_info.constructors)}")
        
        lines.append(f"\n📊 枚举数量: {len(self.reflection_info.enums)}")
        for enum_info in self.reflection_info.enums:
            lines.append(f"\n  🔶 {enum_info.qualified_name}")
            for name, value in enum_info.values:
                lines.append(f"     - {name} = {value}")
        
        lines.append("\n" + "="*70)
        return "\n".join(lines)
    
    # ========================================================================
    # 工具方法
    # ========================================================================
    def _should_generate(self, class_info: ClassInfo) -> bool:
        """判断是否应该为该类生成反射代码"""
        # 跳过没有成员的类
        if not class_info.fields and not class_info.methods:
            return False
        
        # 跳过 refl 命名空间中的标记类
        if class_info.namespace == "refl":
            return False
        
        return True
    
    def _clean_type(self, type_str: str) -> str:
        """清理类型名称"""
        return ' '.join(type_str.split())
