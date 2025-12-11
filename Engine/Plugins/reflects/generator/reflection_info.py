#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
反射信息数据结构

定义用于存储 C++ 类型反射信息的数据类
"""

from typing import List, Dict, Tuple


# ============================================================================
# 反射信息容器
# ============================================================================
class ReflectionInfo:
    """存储所有反射信息的容器"""
    
    def __init__(self):
        self.classes: List['ClassInfo'] = []
        self.enums: List['EnumInfo'] = []


# ============================================================================
# 类信息
# ============================================================================
class ClassInfo:
    """C++ 类/结构体信息"""
    
    def __init__(self, name: str, qualified_name: str):
        self.name = name                                # 类名
        self.qualified_name = qualified_name            # 完全限定名（包含命名空间）
        self.namespace = ""                              # 命名空间
        self.fields: List['FieldInfo'] = []             # 字段列表
        self.methods: List['MethodInfo'] = []           # 方法列表
        self.constructors: List['MethodInfo'] = []      # 构造函数列表
        self.base_classes: List[str] = []               # 基类列表
        self.annotations: Dict[str, str] = {}           # 注解/属性
        
    def __repr__(self):
        return f"ClassInfo({self.qualified_name})"


# ============================================================================
# 字段信息
# ============================================================================
class FieldInfo:
    """类成员字段信息"""
    
    def __init__(self, name: str, type_name: str, access: str):
        self.name = name                    # 字段名
        self.type_name = type_name          # 类型名
        self.access = access                # 访问权限: public, private, protected
        self.annotations: Dict[str, str] = {}  # 注解/属性
        
    def __repr__(self):
        return f"Field({self.access} {self.type_name} {self.name})"


# ============================================================================
# 方法信息
# ============================================================================
class MethodInfo:
    """类方法信息"""
    
    def __init__(self, name: str, return_type: str, access: str):
        self.name = name                    # 方法名
        self.return_type = return_type      # 返回类型
        self.access = access                # 访问权限
        self.parameters: List[Tuple[str, str]] = []  # 参数列表: [(type, name), ...]
        self.is_static = False              # 是否静态方法
        self.is_const = False               # 是否 const 方法
        self.is_virtual = False             # 是否虚函数
        self.annotations: Dict[str, str] = {}  # 注解/属性
        
    def __repr__(self):
        return f"Method({self.access} {self.return_type} {self.name})"


# ============================================================================
# 枚举信息
# ============================================================================
class EnumInfo:
    """C++ 枚举信息"""
    
    def __init__(self, name: str, qualified_name: str):
        self.name = name                        # 枚举名
        self.qualified_name = qualified_name    # 完全限定名
        self.values: List[Tuple[str, int]] = [] # 枚举值: [(name, value), ...]
        self.is_class_enum = False              # 是否为 enum class
        self.annotations: Dict[str, str] = {}   # 注解/属性
        
    def __repr__(self):
        return f"EnumInfo({self.qualified_name})"
