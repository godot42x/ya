# UI 类型自动注册系统

## 概述

UI 类型自动注册系统通过 **inline 静态变量** 和 **lambda 表达式** 自动将所有 UI 类型的继承关系注册到 `UIManager::inheritanceMap` 中。

**无需额外的 .cpp 文件！完全在头文件中完成！**

## 使用方法

### 1. 定义新的 UI 类型

在头文件中使用 `UI_TYPE` 宏，即可自动注册：

```cpp
// YourUIType.h
#pragma once

#include "Core/UI/UIElement.h"

namespace ya
{

struct YourUIType : public UIElement
{
    UI_TYPE(YourUIType, UIElement);  // 自动注册！无需其他操作！
    
    // 你的成员变量和方法
    void render(UIRenderContext &ctx, layer_idx_t layerId) override;
};

} // namespace ya
```

### 2. 完成！

**就这么简单！** 无需在任何 .cpp 文件中添加代码，继承关系会在程序启动时自动注册。

## 工作原理

### UI_TYPE 宏展开 (C++17)

```cpp
UI_TYPE(YourUIType, UIElement)
```

展开为：

```cpp
using Super = UIElement;

static uint32_t getStaticType() { 
    return ya::type_index_v<YourUIType>; 
}

static uint32_t getStaticBaseType() { 
    return ya::type_index_v<UIElement>; 
}

// 关键：inline static 变量 + lambda 立即执行
inline static bool _auto_registered = []() {
    UIManager::registerInheritance(
        ya::type_index_v<YourUIType>, 
        ya::type_index_v<UIElement>
    );
    return true;
}();
```

### 自动注册流程

1. **编译时**：
   - `inline static` 变量确保只有一个实例
   - lambda 表达式被定义并立即调用 `()`

2. **链接时**：
   - 编译器为每个类型生成唯一的 `_auto_registered` 变量
   - `inline` 关键字允许多个编译单元包含相同定义

3. **程序启动前**：
   - C++ 运行时初始化所有静态变量
   - lambda 执行 `UIManager::registerInheritance()`

4. **完成**：
   - 继承关系已存入 `UIManager::inheritanceMap`
   - `_auto_registered` 值为 `true`（实际不使用）

## 已注册的类型

当前已自动注册的 UI 类型：

| 子类型 | 父类型 | 文件 |
|--------|--------|------|
| UIElement | (root) | UIElement.h |
| UICanvas | UIElement | UICanvas.h |
| UIButton | UIElement | UIButton.h |
| UIPanel | UIElement | UIPanel.h |
| UITextBlock | UIElement | UITextBlock.h |

## 技术细节

### 为什么使用 inline static？

```cpp
// ❌ 没有 inline - 链接错误（多重定义）
static bool _registered = ...;

// ✅ 有 inline - 正确工作
inline static bool _auto_registered = ...;
```

C++17 的 `inline static` 成员变量：
- 可以在类定义中初始化
- 多个编译单元包含时不会链接错误
- 保证只有一个实例
- 不需要在 .cpp 中定义

### 为什么使用 lambda？

```cpp
// ✅ Lambda 立即执行
inline static bool _auto_registered = []() {
    UIManager::registerInheritance(...);
    return true;
}();  // 注意这里的 ()

// ❌ 如果没有 ()，只是声明了一个 lambda，不会执行
inline static auto _auto_registered = []() { ... };
```

Lambda 的 `()` 让函数立即执行，在静态初始化阶段完成注册。

### 获取父类型

```cpp
uint32_t childType = UITextBlock::getStaticType();
auto& map = UIManager::get()->inheritanceMap;

if (map.contains(childType)) {
    uint32_t parentType = map[childType];
    // parentType == UIElement::getStaticType()
}
```

### 检查继承关系

```cpp
bool isChildOf(uint32_t childType, uint32_t ancestorType) {
    auto& map = UIManager::get()->inheritanceMap;
    
    uint32_t current = childType;
    while (map.contains(current)) {
        if (current == ancestorType) {
            return true;
        }
        current = map[current];
    }
    return false;
}
```

## UI_ROOT_TYPE 宏

对于没有父类的根类型（如 `UIElement`），使用 `UI_ROOT_TYPE`：

```cpp
struct UIElement
{
    UI_ROOT_TYPE(UIElement);  // 不会注册到 inheritanceMap
    
    // ...
};
```

## 注意事项

1. **必须在 UITypeRegistry.cpp 中添加**：仅在头文件中使用 `UI_TYPE` 不会自动注册，必须在 `.cpp` 中定义静态变量
2. **单继承限制**：当前系统只支持单继承（一个类只有一个直接父类）
3. **链接依赖**：确保 `UITypeRegistry.cpp` 被编译并链接到最终程序中
4. **初始化顺序**：静态初始化在 `main()` 之前，无法控制精确顺序

## 对比手动注册

### ❌ 手动注册（不推荐）

```cpp
void UIManager::init() {
    inheritanceMap[type_index_v<UICanvas>] = type_index_v<UIElement>;
    inheritanceMap[type_index_v<UIButton>] = type_index_v<UIElement>;
    inheritanceMap[type_index_v<UIPanel>] = type_index_v<UIElement>;
    // 容易忘记添加新类型
}
```

### ✅ 自动注册（推荐）

```cpp
// 在头文件中
UI_TYPE(UICanvas, UIElement);

// 在 UITypeRegistry.cpp 中
bool UICanvas::_registered = UICanvas::registerInheritance();

// 自动注册，不会遗漏！
```

## 扩展性

### 添加多级继承

```cpp
// UIElement (root)
//   └─ UIPanel
//       └─ UIScrollPanel

struct UIScrollPanel : public UIPanel
{
    UI_TYPE(UIScrollPanel, UIPanel);  // 父类是 UIPanel，不是 UIElement
};

// UITypeRegistry.cpp
bool UIScrollPanel::_registered = UIScrollPanel::registerInheritance();

// inheritanceMap:
// UIScrollPanel -> UIPanel
// UIPanel -> UIElement
```

### 添加类型元数据

如果需要更多类型信息，可以扩展注册系统：

```cpp
struct UITypeInfo {
    uint32_t typeId;
    uint32_t parentId;
    const char* name;
    size_t size;
};

// 在 UIManager 中
std::unordered_map<uint32_t, UITypeInfo> typeInfoMap;
```

## 常见问题

### Q: 为什么不用虚函数？

A: 虚函数需要对象实例，而 `getStaticType()` 是静态方法，可以在没有实例的情况下获取类型信息。

### Q: 能否自动生成 UITypeRegistry.cpp？

A: 可以！未来可以使用 Python 脚本扫描所有 UI 头文件，自动生成注册代码。

### Q: 如果忘记在 .cpp 中添加注册会怎样？

A: 链接器会报错：`undefined reference to UIYourType::_registered`。这是一个特性，确保不会遗漏注册。

### Q: 能否在运行时动态注册？

A: 可以，但失去了自动化优势。保留 `UIManager::registerInheritance()` 公有接口，允许手动注册。
