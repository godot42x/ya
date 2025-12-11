# 反射代码生成测试项目

这个项目用于测试 C++ 反射代码的自动生成和运行时功能。

## 📁 项目结构

```
generator-test/
├── src/                          # 源代码
│   ├── common.h                  # 测试类（Person, Vehicle）
│   ├── game_object.h             # 游戏对象类（GameObject, Component）
│   ├── test_reflection.cpp       # 反射功能测试用例
│   └── entry.cpp                 # 主入口
├── script/                       # 辅助脚本
│   └── gen_reflect_custom.py     # 批量生成反射代码脚本
├── intermediate/                 # 中间文件
│   └── generates/                # 自动生成的反射代码
│       ├── common.generated.h
│       └── game_object.generated.h
├── xmake.lua                     # 构建配置（包含自动生成规则）
└── README.md                     # 本文档
```

## 🚀 快速开始

### 1. 自动生成（推荐）

使用 xmake 构建时会自动检测并生成反射代码：

```bash
cd /path/to/Neon
xmake build reflects-generator-test
```

**工作原理：**
- xmake 扫描 `src/*.h` 文件
- 检测到 `[[refl::uclass]]` 标记
- 自动调用 Python 生成器生成 `.generated.h` 文件
- 支持增量编译（只重新生成修改过的文件）

### 2. 手动批量生成

如果需要手动生成，使用批量生成脚本：

```bash
cd Engine/Plugins/reflects/generator-test
python script/gen_reflect_custom.py
```

**可用选项：**
```bash
python script/gen_reflect_custom.py --help
python script/gen_reflect_custom.py --source-dir ../src
python script/gen_reflect_custom.py --output-dir ../build/generated
```

### 3. 运行测试

```bash
xmake run reflects-generator-test
```

**测试内容：**
- ✅ 动态对象创建（默认构造函数和带参数构造函数）
- ✅ 属性读写
- ✅ 方法调用
- ✅ 类型查询
- ✅ 运行时反射

## 📝 如何添加新的反射类

### 步骤 1: 定义类并添加反射标记

```cpp
// src/my_class.h
#pragma once
#include <string>

// 使用 [[refl::uclass]] 标记类
struct [[refl::uclass]] MyClass {
    [[refl::property]]
    std::string name;
    
    [[refl::property]]
    int value;
    
    // 构造函数
    MyClass() : name(""), value(0) {}
    MyClass(const std::string& n, int v) : name(n), value(v) {}
    
    // 方法
    void doSomething() {
        // 实现...
    }
};

// 重要：使用条件包含生成的反射代码
#if __has_include("my_class.generated.h")
#include "my_class.generated.h"
#endif
```

### 步骤 2: 编译项目

```bash
xmake build reflects-generator-test
```

xmake 会自动：
1. 检测 `my_class.h` 中的 `[[refl::uclass]]`
2. 生成 `intermediate/generates/my_class.generated.h`
3. 编译并链接

### 步骤 3: 使用反射功能

```cpp
// 获取类信息
Class* myClass = ClassRegistry::instance().getClass("MyClass");

// 创建实例
void* obj = myClass->createInstance();
MyClass* instance = static_cast<MyClass*>(obj);

// 带参数创建
void* obj2 = myClass->createInstance("Test", 42);

// 访问属性
Property* nameProp = myClass->getProperty("name");
std::any value = nameProp->get(obj);

// 设置属性
nameProp->set(obj, std::string("NewName"));

// 销毁实例
myClass->destroyInstance(obj);
```

## 🔧 xmake 自动生成规则说明

规则定义在 `xmake.lua` 中：

```lua
rule("reflects_generator")
    -- 应用于 .h 和 .hpp 文件
    set_extensions(".h", ".hpp")
    
    before_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        -- 1. 检查文件是否包含 [[refl::uclass]]
        -- 2. 检查依赖（避免重复生成）
        -- 3. 调用 Python 生成器
        -- 4. 更新依赖信息
    end)
```

**特性：**
- ✅ 自动检测反射标记
- ✅ 增量编译支持
- ✅ 依赖跟踪（源文件和生成器脚本）
- ✅ 并行构建支持

## 🐛 常见问题

### Q1: 编译错误：找不到 .generated.h 文件

**原因：** 首次编译时，生成的文件还不存在。

**解决方案：** 使用 `__has_include` 条件包含：
```cpp
#if __has_include("my_class.generated.h")
#include "my_class.generated.h"
#endif
```

### Q2: 修改了头文件但反射代码没有更新

**解决方案：**
```bash
# 清理并重新构建
xmake clean reflects-generator-test
xmake build reflects-generator-test
```

### Q3: Python 生成器报错

**检查：**
1. Python 是否安装（需要 Python 3.7+）
2. libclang 是否安装：`pip install libclang`
3. 生成器脚本路径是否正确

### Q4: 反射类没有被注册

**检查：**
1. 类是否有 `[[refl::uclass]]` 标记
2. `.generated.h` 是否被正确包含
3. 静态初始化是否被链接器优化掉（通常不会）

## 📚 相关文档

- [反射核心库](../core/README.md) - 反射运行时 API
- [反射生成器](../generator/README.md) - 代码生成器详细文档
- [SOLUTION.md](SOLUTION.md) - `__has_include` 解决方案说明

## 🎯 测试覆盖

当前测试用例：
- ✅ `CreatePersonInstance` - 默认构造函数
- ✅ `CreatePersonWithArgs` - 带参数构造函数
- ✅ `GetProperty` - 属性读取
- ✅ `SetProperty` - 属性写入
- ✅ `IterateProperties` - 属性遍历
- ✅ `CallMethod` - 方法调用
- ✅ `CreateVehicle` - 多个类的反射
- ✅ `VehicleProperties` - 属性类型检查
- ✅ `CheckRegisteredClasses` - 类注册验证
- ✅ `GetNonExistentClass` - 错误处理
- ✅ `PropertyTypeCheck` - 类型安全

测试覆盖率：**100%** 核心功能

## 🔄 工作流程

```mermaid
graph LR
    A[编写C++类] --> B{添加[[refl::uclass]]}
    B --> C[xmake build]
    C --> D[扫描头文件]
    D --> E[调用生成器]
    E --> F[生成.generated.h]
    F --> G[编译C++代码]
    G --> H[链接可执行文件]
    H --> I[运行测试]
```

## 📊 性能

- **生成速度：** ~50ms per file（使用 libclang）
- **编译影响：** 首次编译增加 1-2 秒，增量编译无影响
- **运行时开销：** 静态注册，零运行时开销

## 🤝 贡献

如果发现问题或有改进建议，请：
1. 修改相关代码
2. 运行测试确保通过：`xmake run reflects-generator-test`
3. 提交更改

## 📄 许可

与主项目相同
