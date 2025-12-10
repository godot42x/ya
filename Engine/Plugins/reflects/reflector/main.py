#!/usr/bin/env python3
"""
C++ Reflection Code Generator using libclang
Parses C++ header files and generates reflection metadata
"""

import os
import sys
import argparse
from pathlib import Path
from typing import List, Dict, Set, Optional

def check_modules(modules):
    """Check and install required Python modules"""
    missing_modules = []
    for module in modules:
        try:
            __import__(module)
        except ImportError:
            missing_modules.append(module)

    # install missing modules
    for module in missing_modules:
        print(f"Installing missing module: {module}")
        os.system(f"python -m pip install {module}")

check_modules(["clang"])

import clang.cindex
from clang.cindex import CursorKind, TypeKind, AccessSpecifier


class ReflectionInfo:
    """Stores reflection information for C++ types"""
    
    def __init__(self):
        self.classes: List['ClassInfo'] = []
        self.enums: List['EnumInfo'] = []
        
        
class ClassInfo:
    """Information about a C++ class/struct"""
    
    def __init__(self, name: str, qualified_name: str):
        self.name = name
        self.qualified_name = qualified_name
        self.fields: List['FieldInfo'] = []
        self.methods: List['MethodInfo'] = []
        self.base_classes: List[str] = []
        self.annotations: Dict[str, str] = {}
        self.namespace = ""
        
    def __repr__(self):
        return f"ClassInfo({self.qualified_name})"


class FieldInfo:
    """Information about a class field/member variable"""
    
    def __init__(self, name: str, type_name: str, access: str):
        self.name = name
        self.type_name = type_name
        self.access = access  # public, private, protected
        self.annotations: Dict[str, str] = {}
        
    def __repr__(self):
        return f"Field({self.access} {self.type_name} {self.name})"


class MethodInfo:
    """Information about a class method"""
    
    def __init__(self, name: str, return_type: str, access: str):
        self.name = name
        self.return_type = return_type
        self.access = access
        self.parameters: List[tuple] = []  # [(type, name), ...]
        self.is_static = False
        self.is_const = False
        self.is_virtual = False
        self.annotations: Dict[str, str] = {}
        
    def __repr__(self):
        return f"Method({self.access} {self.return_type} {self.name})"


class EnumInfo:
    """Information about a C++ enum"""
    
    def __init__(self, name: str, qualified_name: str):
        self.name = name
        self.qualified_name = qualified_name
        self.values: List[tuple] = []  # [(name, value), ...]
        self.is_class_enum = False
        self.annotations: Dict[str, str] = {}
        
    def __repr__(self):
        return f"EnumInfo({self.qualified_name})"


class CppReflector:
    """Main class for parsing C++ files and extracting reflection information"""
    
    def __init__(self, include_paths: List[str] = None, defines: List[str] = None):
        self.index = clang.cindex.Index.create()
        self.include_paths = include_paths or []
        self.defines = defines or []
        self.reflection_info = ReflectionInfo()
        
    def parse_file(self, filename: str, additional_args: List[str] = None) -> bool:
        """Parse a C++ header file"""
        args = ['-x', 'c++', '-std=c++17']
        
        # Add include paths
        for include_path in self.include_paths:
            args.append(f'-I{include_path}')
            
        # Add defines
        for define in self.defines:
            args.append(f'-D{define}')
            
        if additional_args:
            args.extend(additional_args)
            
        print(f"Parsing {filename}...")
        print(f"Clang args: {' '.join(args)}")
        
        try:
            tu = self.index.parse(filename, args=args)
            
            # Check for errors
            errors = []
            for diag in tu.diagnostics:
                if diag.severity >= clang.cindex.Diagnostic.Error:
                    errors.append(str(diag))
                    
            if errors:
                print("Parsing errors:")
                for error in errors:
                    print(f"  {error}")
                return False
                
            # Process the AST
            self._process_cursor(tu.cursor, filename)
            return True
            
        except Exception as e:
            print(f"Error parsing {filename}: {e}")
            return False
            
    def _process_cursor(self, cursor, filename: str, namespace: str = ""):
        """Recursively process AST nodes"""
        
        # Only process nodes from the target file
        if cursor.location.file and cursor.location.file.name != filename:
            return
            
        # Check for REFLECT annotation in comments
        has_reflect = self._has_reflect_annotation(cursor)
        
        if cursor.kind == CursorKind.NAMESPACE:
            # Enter namespace
            new_namespace = f"{namespace}::{cursor.spelling}" if namespace else cursor.spelling
            for child in cursor.get_children():
                self._process_cursor(child, filename, new_namespace)
                
        elif cursor.kind in (CursorKind.CLASS_DECL, CursorKind.STRUCT_DECL):
            if has_reflect or self._should_reflect_class(cursor):
                class_info = self._extract_class_info(cursor, namespace)
                if class_info:
                    self.reflection_info.classes.append(class_info)
                    
        elif cursor.kind == CursorKind.ENUM_DECL:
            if has_reflect or self._should_reflect_enum(cursor):
                enum_info = self._extract_enum_info(cursor, namespace)
                if enum_info:
                    self.reflection_info.enums.append(enum_info)
                    
        # Continue traversing
        for child in cursor.get_children():
            self._process_cursor(child, filename, namespace)
            
    def _has_reflect_annotation(self, cursor) -> bool:
        """Check if cursor has REFLECT annotation in comments"""
        # Check raw comment
        comment = cursor.raw_comment
        if comment and 'REFLECT' in comment:
            return True
        return False
        
    def _should_reflect_class(self, cursor) -> bool:
        """Determine if a class should be reflected"""
        # You can add custom logic here
        # For now, reflect all classes that have at least one member
        return cursor.get_definition() is not None
        
    def _should_reflect_enum(self, cursor) -> bool:
        """Determine if an enum should be reflected"""
        return True
        
    def _extract_class_info(self, cursor, namespace: str) -> Optional[ClassInfo]:
        """Extract information from a class/struct declaration"""
        name = cursor.spelling
        qualified_name = f"{namespace}::{name}" if namespace else name
        
        class_info = ClassInfo(name, qualified_name)
        class_info.namespace = namespace
        
        # Extract base classes
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_BASE_SPECIFIER:
                base_name = child.type.spelling
                class_info.base_classes.append(base_name)
                
        # Extract fields and methods
        current_access = "private" if cursor.kind == CursorKind.CLASS_DECL else "public"
        
        for child in cursor.get_children():
            if child.kind == CursorKind.CXX_ACCESS_SPEC_DECL:
                current_access = self._get_access_specifier(child)
                
            elif child.kind == CursorKind.FIELD_DECL:
                field = self._extract_field_info(child, current_access)
                if field:
                    class_info.fields.append(field)
                    
            elif child.kind in (CursorKind.CXX_METHOD, CursorKind.CONSTRUCTOR, CursorKind.DESTRUCTOR):
                method = self._extract_method_info(child, current_access)
                if method:
                    class_info.methods.append(method)
                    
        return class_info
        
    def _extract_field_info(self, cursor, access: str) -> Optional[FieldInfo]:
        """Extract information from a field declaration"""
        name = cursor.spelling
        type_name = cursor.type.spelling
        
        field = FieldInfo(name, type_name, access)
        return field
        
    def _extract_method_info(self, cursor, access: str) -> Optional[MethodInfo]:
        """Extract information from a method declaration"""
        name = cursor.spelling
        return_type = cursor.result_type.spelling
        
        method = MethodInfo(name, return_type, access)
        method.is_static = cursor.is_static_method()
        method.is_const = cursor.is_const_method()
        method.is_virtual = cursor.is_virtual_method()
        
        # Extract parameters
        for arg in cursor.get_arguments():
            param_type = arg.type.spelling
            param_name = arg.spelling
            method.parameters.append((param_type, param_name))
            
        return method
        
    def _extract_enum_info(self, cursor, namespace: str) -> Optional[EnumInfo]:
        """Extract information from an enum declaration"""
        name = cursor.spelling or "anonymous"
        qualified_name = f"{namespace}::{name}" if namespace else name
        
        enum_info = EnumInfo(name, qualified_name)
        enum_info.is_class_enum = cursor.kind == CursorKind.ENUM_DECL
        
        # Extract enum values
        for child in cursor.get_children():
            if child.kind == CursorKind.ENUM_CONSTANT_DECL:
                enum_name = child.spelling
                enum_value = child.enum_value
                enum_info.values.append((enum_name, enum_value))
                
        return enum_info
        
    def _get_access_specifier(self, cursor) -> str:
        """Get access specifier from cursor"""
        access = cursor.access_specifier
        if access == AccessSpecifier.PUBLIC:
            return "public"
        elif access == AccessSpecifier.PROTECTED:
            return "protected"
        elif access == AccessSpecifier.PRIVATE:
            return "private"
        return "public"


class CodeGenerator:
    """Generate reflection code from reflection info"""
    
    def __init__(self, reflection_info: ReflectionInfo):
        self.reflection_info = reflection_info
        
    def generate_header(self, output_file: str):
        """Generate reflection header file"""
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write("// Auto-generated reflection code\n")
            f.write("// DO NOT EDIT\n\n")
            f.write("#pragma once\n\n")
            f.write("#include <string>\n")
            f.write("#include <vector>\n")
            f.write("#include <unordered_map>\n")
            f.write("#include <cstddef>\n")
            f.write("#include <any>\n")
            f.write("#include <functional>\n")
            f.write("#include <stdexcept>\n\n")
            
            # Include the reflection core types
            f.write("// Reflection core types\n")
            f.write("#ifndef REFLECTION_CORE_TYPES\n")
            f.write("#define REFLECTION_CORE_TYPES\n")
            f.write("namespace Reflection {\n")
            f.write("    using Variant = std::any;\n")
            f.write("    class Type;\n")
            f.write("    class Method;\n")
            f.write("    class Field;\n")
            f.write("    template<typename T>\n")
            f.write("    T VariantCast(const Variant& v) {\n")
            f.write("        try {\n")
            f.write("            return std::any_cast<T>(v);\n")
            f.write("        } catch (const std::bad_any_cast& e) {\n")
            f.write("            throw std::runtime_error(std::string(\"Failed to cast variant: \") + e.what());\n")
            f.write("        }\n")
            f.write("    }\n")
            f.write("}\n")
            f.write("#endif\n\n")
            
            # Generate enum reflection
            for enum_info in self.reflection_info.enums:
                self._generate_enum_reflection(f, enum_info)
                
            # Generate class reflection
            for class_info in self.reflection_info.classes:
                self._generate_class_reflection(f, class_info)
                
    def _generate_enum_reflection(self, f, enum_info: EnumInfo):
        """Generate reflection code for an enum"""
        f.write(f"// Reflection for enum {enum_info.qualified_name}\n")
        f.write(f"namespace Reflection {{\n\n")
        
        # Generate string conversion
        f.write(f"inline const char* ToString({enum_info.qualified_name} value) {{\n")
        f.write(f"    switch(value) {{\n")
        for name, value in enum_info.values:
            f.write(f"        case {enum_info.qualified_name}::{name}: return \"{name}\";\n")
        f.write(f"        default: return \"Unknown\";\n")
        f.write(f"    }}\n")
        f.write(f"}}\n\n")
        
        f.write(f"}} // namespace Reflection\n\n")
        
    def _generate_class_reflection(self, f, class_info: ClassInfo):
        """Generate reflection code for a class"""
        f.write(f"// Reflection for class {class_info.qualified_name}\n")
        f.write(f"namespace Reflection {{\n\n")
        
        # Generate field info
        f.write(f"struct {class_info.name}FieldInfo {{\n")
        f.write(f"    const char* name;\n")
        f.write(f"    const char* type;\n")
        f.write(f"    size_t offset;\n")
        f.write(f"}};\n\n")
        
        # Generate field list
        if class_info.fields:
            f.write(f"inline const std::vector<{class_info.name}FieldInfo>& Get{class_info.name}Fields() {{\n")
            f.write(f"    static std::vector<{class_info.name}FieldInfo> fields = {{\n")
            for field in class_info.fields:
                if field.access == "public":  # Only public fields
                    f.write(f"        {{\"{field.name}\", \"{field.type_name}\", offsetof({class_info.qualified_name}, {field.name})}},\n")
            f.write(f"    }};\n")
            f.write(f"    return fields;\n")
            f.write(f"}}\n\n")
        
        # Generate method info structure
        if class_info.methods:
            f.write(f"struct {class_info.name}MethodInfo {{\n")
            f.write(f"    const char* name;\n")
            f.write(f"    const char* returnType;\n")
            f.write(f"    std::vector<std::pair<const char*, const char*>> parameters;\n")
            f.write(f"    bool isStatic;\n")
            f.write(f"    bool isConst;\n")
            f.write(f"}};\n\n")
        
        # Generate method invokers for each public method
        for method in class_info.methods:
            if method.access == "public":
                self._generate_method_invoker(f, class_info, method)
        
        # Generate method registration function
        if class_info.methods:
            public_methods = [m for m in class_info.methods if m.access == "public"]
            if public_methods:
                f.write(f"inline void Register{class_info.name}Methods(Type* type) {{\n")
                for method in public_methods:
                    self._generate_method_registration(f, class_info, method)
                f.write(f"}}\n\n")
            
        f.write(f"}} // namespace Reflection\n\n")
    
    def _generate_method_invoker(self, f, class_info: ClassInfo, method: MethodInfo):
        """Generate a method invoker function for dynamic invocation"""
        method_name = method.name
        safe_name = method_name.replace("~", "Destructor_")
        
        # Skip destructors and operators for now
        if method_name.startswith("~") or method_name.startswith("operator"):
            return
        
        param_count = len(method.parameters)
        
        f.write(f"// Invoker for {method_name}\n")
        f.write(f"inline Variant Invoke_{class_info.name}_{safe_name}_{param_count}(void* instance, const std::vector<Variant>& args) {{\n")
        
        # Cast instance
        f.write(f"    auto* obj = static_cast<{class_info.qualified_name}*>(instance);\n")
        
        # Extract arguments
        for i, (param_type, param_name) in enumerate(method.parameters):
            # Clean up parameter type
            clean_type = param_type.replace("const ", "").replace("&", "").replace("*", "").strip()
            f.write(f"    auto arg{i} = VariantCast<{param_type}>(args[{i}]);\n")
        
        # Call method and return result
        arg_list = ", ".join([f"arg{i}" for i in range(param_count)])
        
        if method.return_type == "void":
            f.write(f"    obj->{method_name}({arg_list});\n")
            f.write(f"    return Variant();\n")
        else:
            f.write(f"    auto result = obj->{method_name}({arg_list});\n")
            f.write(f"    return Variant(result);\n")
        
        f.write(f"}}\n\n")
    
    def _generate_method_registration(self, f, class_info: ClassInfo, method: MethodInfo):
        """Generate method registration code"""
        method_name = method.name
        safe_name = method_name.replace("~", "Destructor_")
        
        # Skip destructors and operators
        if method_name.startswith("~") or method_name.startswith("operator"):
            return
        
        param_count = len(method.parameters)
        
        f.write(f"    {{\n")
        f.write(f"        auto* method = new Method(\"{method_name}\", \"{method.return_type}\");\n")
        f.write(f"        method->SetStatic({str(method.is_static).lower()});\n")
        f.write(f"        method->SetConst({str(method.is_const).lower()});\n")
        
        # Add parameters
        for param_type, param_name in method.parameters:
            actual_name = param_name if param_name else "arg"
            f.write(f"        method->AddParameter(\"{param_type}\", \"{actual_name}\");\n")
        
        # Set invoke function
        f.write(f"        method->SetInvokeFunc(Invoke_{class_info.name}_{safe_name}_{param_count});\n")
        f.write(f"        type->AddMethod(method);\n")
        f.write(f"    }}\n")

        
    def generate_implementation(self, output_file: str):
        """Generate reflection implementation file"""
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write("// Auto-generated reflection implementation\n")
            f.write("// DO NOT EDIT\n\n")
            f.write("#include \"reflection.generated.h\"\n\n")
            
            # Additional implementation can go here
            
    def generate_report(self) -> str:
        """Generate a human-readable report"""
        lines = ["Reflection Report", "=" * 50, ""]
        
        lines.append(f"Classes: {len(self.reflection_info.classes)}")
        for class_info in self.reflection_info.classes:
            lines.append(f"  - {class_info.qualified_name}")
            lines.append(f"    Fields: {len(class_info.fields)}")
            for field in class_info.fields:
                lines.append(f"      {field}")
            lines.append(f"    Methods: {len(class_info.methods)}")
            for method in class_info.methods:
                lines.append(f"      {method}")
            lines.append("")
            
        lines.append(f"Enums: {len(self.reflection_info.enums)}")
        for enum_info in self.reflection_info.enums:
            lines.append(f"  - {enum_info.qualified_name}")
            for name, value in enum_info.values:
                lines.append(f"      {name} = {value}")
            lines.append("")
            
        return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description='C++ Reflection Code Generator')
    parser.add_argument('input', nargs='+', help='Input C++ header files')
    parser.add_argument('-o', '--output', default='reflection.generated.h', help='Output header file')
    parser.add_argument('-I', '--include', action='append', default=[], help='Include directories')
    parser.add_argument('-D', '--define', action='append', default=[], help='Preprocessor definitions')
    parser.add_argument('--report', action='store_true', help='Print reflection report')
    parser.add_argument('--libclang', help='Path to libclang library')
    
    args = parser.parse_args()
    
    # Set libclang library path if provided
    if args.libclang:
        clang.cindex.Config.set_library_file(args.libclang)
    
    # Create reflector
    reflector = CppReflector(include_paths=args.include, defines=args.define)
    
    # Parse all input files
    success = True
    for input_file in args.input:
        if not reflector.parse_file(input_file):
            success = False
            
    if not success:
        print("Parsing failed!")
        return 1
        
    # Generate code
    generator = CodeGenerator(reflector.reflection_info)
    
    print(f"Generating {args.output}...")
    generator.generate_header(args.output)
    
    # Generate report if requested
    if args.report:
        print("\n" + generator.generate_report())
        
    print(f"Success! Generated reflection code for {len(reflector.reflection_info.classes)} classes and {len(reflector.reflection_info.enums)} enums")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
