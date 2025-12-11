#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C++ 代码解析器

使用 libclang 解析 C++ 代码并提取反射信息
"""

import re
from typing import List, Dict, Optional

try:
    import clang.cindex
    from clang.cindex import CursorKind, AccessSpecifier
except ImportError:
    print("警告: libclang 未安装，请运行: pip install libclang")
    raise

from reflection_info import (
    ReflectionInfo, ClassInfo, FieldInfo, MethodInfo, EnumInfo
)


# ============================================================================
# C++ 解析器
# ============================================================================
class CppParser:
    """使用 libclang 解析 C++ 头文件"""
    
    def __init__(self, include_paths: List[str] = None, defines: List[str] = None):
        """
        初始化解析器
        
        参数:
            include_paths: 包含目录列表
            defines: 预处理器定义列表
        """
        self.index = clang.cindex.Index.create()
        self.include_paths = include_paths or []
        self.defines = defines or []
        self.reflection_info = ReflectionInfo()
        
    def parse_file(self, filename: str, additional_args: List[str] = None) -> bool:
        """
        解析 C++ 头文件
        
        参数:
            filename: 头文件路径
            additional_args: 额外的编译参数
            
        返回:
            成功返回 True,失败返回 False
        """
        # 构建编译参数
        args = ['-x', 'c++', '-std=c++23', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH']
        
        for include_path in self.include_paths:
            args.append(f'-I{include_path}')
            
        for define in self.defines:
            args.append(f'-D{define}')
            
        if additional_args:
            args.extend(additional_args)
            
        print(f"[Parse] Parsing file: {filename}")
        print(f"   参数: {' '.join(args)}")
        
        try:
            # 解析文件
            tu = self.index.parse(filename, args=args)
            
            # 检查错误
            errors = []
            for diag in tu.diagnostics:
                if diag.severity >= clang.cindex.Diagnostic.Error:
                    errors.append(str(diag))
                    
            if errors:
                print("[ERROR] Parse errors:")
                for error in errors:
                    print(f"   {error}")
                return False
                
            # 遍历 AST
            self._process_cursor(tu.cursor, filename)
            return True
            
        except Exception as e:
            print(f"[ERROR] Parse exception: {e}")
            return False
    
    # ========================================================================
    # AST 遍历
    # ========================================================================
    def _process_cursor(self, cursor, filename: str, namespace: str = ""):
        """递归处理 AST 节点"""
        # 只处理目标文件中的节点
        if cursor.location.file and cursor.location.file.name != filename:
            return
            
        # 处理不同类型的节点
        if cursor.kind == CursorKind.NAMESPACE:
            self._process_namespace(cursor, filename, namespace)
            
        elif cursor.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
            self._process_class(cursor, namespace)
            
        elif cursor.kind == CursorKind.ENUM_DECL:
            self._process_enum(cursor, namespace)
            
        # 继续遍历子节点
        for child in cursor.get_children():
            self._process_cursor(child, filename, namespace)
    
    def _process_namespace(self, cursor, filename: str, namespace: str):
        """处理命名空间"""
        new_namespace = f"{namespace}::{cursor.spelling}" if namespace else cursor.spelling
        for child in cursor.get_children():
            self._process_cursor(child, filename, new_namespace)
    
    def _process_class(self, cursor, namespace: str):
        """处理类/结构体"""
        if cursor.get_definition() is not None:
            class_info = self._extract_class_info(cursor, namespace)
            if class_info:
                self.reflection_info.classes.append(class_info)
    
    def _process_enum(self, cursor, namespace: str):
        """处理枚举"""
        enum_info = self._extract_enum_info(cursor, namespace)
        if enum_info:
            self.reflection_info.enums.append(enum_info)
    
    # ========================================================================
    # 信息提取
    # ========================================================================
    def _extract_class_info(self, cursor, namespace: str) -> Optional[ClassInfo]:
        """提取类信息"""
        name = cursor.spelling
        qualified_name = f"{namespace}::{name}" if namespace else name
        
        class_info = ClassInfo(name, qualified_name)
        class_info.namespace = namespace
        
        # 提取基类
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_BASE_SPECIFIER:
                class_info.base_classes.append(child.type.spelling)
        
        # 提取成员
        current_access = "private" if cursor.kind == CursorKind.CLASS_DECL else "public"
        
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_ACCESS_SPEC_DECL:
                current_access = self._get_access_specifier(child)
                
            elif child.kind == CursorKind.FIELD_DECL:
                field = self._extract_field_info(child, current_access)
                if field:
                    class_info.fields.append(field)
                    
            elif child.kind == CursorKind.CONSTRUCTOR:
                ctor = self._extract_method_info(child, current_access)
                if ctor:
                    class_info.constructors.append(ctor)
                    
            elif child.kind in (CursorKind.CXX_METHOD, CursorKind.DESTRUCTOR):
                method = self._extract_method_info(child, current_access)
                if method:
                    class_info.methods.append(method)
                    
        return class_info
    
    def _extract_field_info(self, cursor, access: str) -> Optional[FieldInfo]:
        """提取字段信息"""
        field = FieldInfo(cursor.spelling, cursor.type.spelling, access)
        field.annotations = self._extract_attributes(cursor)
        return field
    
    def _extract_method_info(self, cursor, access: str) -> Optional[MethodInfo]:
        """提取方法信息"""
        method = MethodInfo(cursor.spelling, cursor.result_type.spelling, access)
        method.is_static = cursor.is_static_method()
        method.is_const = cursor.is_const_method()
        method.is_virtual = cursor.is_virtual_method()
        
        # 提取参数
        for arg in cursor.get_arguments():
            method.parameters.append((arg.type.spelling, arg.spelling))
            
        return method
    
    def _extract_enum_info(self, cursor, namespace: str) -> Optional[EnumInfo]:
        """提取枚举信息"""
        name = cursor.spelling or "anonymous"
        qualified_name = f"{namespace}::{name}" if namespace else name
        
        enum_info = EnumInfo(name, qualified_name)
        enum_info.is_class_enum = cursor.kind == CursorKind.ENUM_DECL
        
        # 提取枚举值
        for child in cursor.get_children():
            if child.kind == CursorKind.ENUM_CONSTANT_DECL:
                enum_info.values.append((child.spelling, child.enum_value))
                
        return enum_info
    
    # ========================================================================
    # 属性提取
    # ========================================================================
    def _extract_attributes(self, cursor) -> Dict[str, str]:
        """提取 C++ 属性 [[...]]"""
        annotations = {}
        
        try:
            extent = cursor.extent
            if extent.start.file and extent.start.file.name:
                with open(extent.start.file.name, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
                    
                start_line = extent.start.line - 1
                end_line = extent.end.line
                search_start = max(0, start_line - 2)
                search_text = ''.join(lines[search_start:end_line])
                
                # 匹配 [[...]] 模式
                attr_pattern = r'\[\[([^\]]+(?:\([^\)]*\))?[^\]]*)\]\]'
                matches = re.finditer(attr_pattern, search_text)
                
                for match in matches:
                    attr_content = match.group(1).strip()
                    attr_name, attr_value = self._parse_attribute_text(attr_content)
                    if attr_name:
                        annotations[attr_name] = attr_value
                        
        except Exception:
            pass
        
        return annotations
    
    def _parse_attribute_text(self, text: str) -> tuple:
        """解析属性文本"""
        text = text.strip()
        if not text:
            return None, ""
        
        if '(' in text:
            parts = text.split('(', 1)
            attr_name = parts[0].strip()
            if len(parts) > 1:
                params = parts[1].rstrip(')')
                return attr_name, params
        
        return text, ""
    
    # ========================================================================
    # 工具方法
    # ========================================================================
    def _get_access_specifier(self, cursor) -> str:
        """获取访问权限"""
        access = cursor.access_specifier
        if access == AccessSpecifier.PUBLIC:
            return "public"
        elif access == AccessSpecifier.PROTECTED:
            return "protected"
        elif access == AccessSpecifier.PRIVATE:
            return "private"
        return "public"
