# 反射宏系统使用指南

## 概览

YA Engine 提供了两种反射宏方案，以适应不同的使用场景：

1. **YA_REFLECT (紧凑式)** - 适用于属性少于 16 个的类型
2. **YA_REFLECT_BEGIN/END (扩展式)** - 适用于任意数量的属性（无限制）

## 方案一：YA_REFLECT（推荐用于小型类）

### 特点
- ✅ 语法简洁
- ✅ 自动生成 visitor
- ❌ 最多支持 16 个属性
- ✅ 适合大多数游戏组件

### 使用示例

```cpp
struct PlayerComponent {
    YA_REFLECT(PlayerComponent,
        PROP(health, .range(0.0f, 100.0f).tooltip("Health")),
        PROP(speed, .range(0.0f, 10.0f)),
        PROP(cache, .transient())
    )

    float health = 100.0f;
    float speed = 5.0f;
    float cache = 0.0f;
};
```

## 方案二：YA_REFLECT_BEGIN/END（大型类专用）

### 特点
- ✅ 无数量限制（可支持 100+ 属性）
- ✅ 适合复杂的游戏对象
- ❌ 需要手动维护 visitor
- ❌ 语法稍显冗长

### 使用示例

```cpp
struct LargeComponent {
    YA_REFLECT_BEGIN(LargeComponent)
        YA_REFLECT_FIELD(field1, .tooltip("Field 1"))
        YA_REFLECT_FIELD(field2, .range(0, 100))
        YA_REFLECT_FIELD(field3, .transient())
        // ... 可以添加任意多个字段
        YA_REFLECT_FIELD(field100, )
    YA_REFLECT_END(LargeComponent)
        // Visitor 部分（需要手动维护）
        YA_VISIT_FIELD(field1)
        YA_VISIT_FIELD(field2)
        YA_VISIT_FIELD(field3)
        // ...
        YA_VISIT_FIELD(field100)
    YA_REFLECT_VISITOR_END(LargeComponent)

    int field1, field2, field3, /* ... */ field100;
};
```

## 外部反射（第三方类型）

### YA_REFLECT_EXTERNAL（紧凑式，≤16 个属性）

```cpp
namespace ThirdParty {
    struct Vector3 {
        float x, y, z;
    };
}

YA_REFLECT_EXTERNAL(ThirdParty::Vector3,
    PROP(x, .tooltip("X coordinate")),
    PROP(y, .tooltip("Y coordinate")),
    PROP(z, .tooltip("Z coordinate"))
)
```

### YA_REFLECT_EXTERNAL_BEGIN/END（扩展式，无限制）

```cpp
namespace ThirdParty {
    struct Matrix4x4 {
        float m00, m01, m02, m03;
        float m10, m11, m12, m13;
        float m20, m21, m22, m23;
        float m30, m31, m32, m33;
    };
}

YA_REFLECT_EXTERNAL_BEGIN(ThirdParty::Matrix4x4)
    YA_REFLECT_EXTERNAL_FIELD(m00, .tooltip("Element [0,0]"))
    YA_REFLECT_EXTERNAL_FIELD(m01, .tooltip("Element [0,1]"))
    // ... 16 个字段，突破了 YA_REFLECT_EXTERNAL 的限制
    YA_REFLECT_EXTERNAL_FIELD(m33, .tooltip("Element [3,3]"))
YA_REFLECT_EXTERNAL_END(ThirdParty::Matrix4x4)
    YA_VISIT_EXTERNAL_FIELD(m00)
    YA_VISIT_EXTERNAL_FIELD(m01)
    // ...
    YA_VISIT_EXTERNAL_FIELD(m33)
YA_REFLECT_EXTERNAL_VISITOR_END(ThirdParty::Matrix4x4)
```

## 元数据 API

所有方案都支持以下元数据：

```cpp
.tooltip("描述文本")           // 提示信息
.range(min, max)               // 数值范围
.category("分类")              // 属性分类
.displayName("显示名称")       // 自定义显示名
.transient()                   // 标记为临时数据（不序列化）
```

## 技术实现

### 可变参宏系统

两种方案都基于通用的可变参宏工具 ([VariadicMacros.h](../Macro/VariadicMacros.h))：

- `YA_FOREACH(operation, context, ...)` - 自动展开 1-16 个参数
- `YA_STRINGIZE(x)` - 字符串化宏
- `YA_CONCAT(a, b)` - 符号连接宏

### 架构层次

```
可变参宏工具 (VariadicMacros.h)
    ↓
反射操作宏 (UnifiedReflection.h)
    ↓
用户接口宏 (YA_REFLECT, YA_REFLECT_BEGIN/END)
```

## 性能考虑

| 方案 | 编译开销 | 运行时开销 | 推荐场景 |
|------|---------|-----------|---------|
| YA_REFLECT | 低 | 无 | 1-16 属性 |
| YA_REFLECT_BEGIN/END | 中 | 无 | 17+ 属性 |
| 代码生成器 | 无（离线） | 无 | 100+ 属性 |

## 未来方向

### 短期方案（当前）
使用 `YA_REFLECT_BEGIN/END` 处理大型类型

### 长期方案（推荐）
转向代码生成器 + 注解系统：

1. **Clang LibTooling** - AST 解析
2. **自定义属性** - `[[reflect]]` 标记
3. **自动生成** - 生成反射代码

示例：
```cpp
struct [[reflect]] PlayerComponent {
    [[field]] float health = 100.0f;
    [[field]] float speed = 5.0f;
    [[transient]] float cache = 0.0f;
};

// 通过工具自动生成 PlayerComponent_Reflection.cpp
```

### C++26 方案（未来）
等待 **P2996 Static Reflection** 标准化，使用编译期反射：

```cpp
// 未来的 C++26 代码
template<typename T>
void serialize(T& obj) {
    constexpr auto members = std::meta::members_of(^T);
    for (auto member : members) {
        // 编译期遍历所有成员
    }
}
```

## 最佳实践

### 1. 优先使用 YA_REFLECT
如果属性少于 16 个，始终使用 `YA_REFLECT`

### 2. 重构大类
如果类有超过 16 个属性，考虑：
- 拆分为多个小类（**推荐**）
- 使用 YA_REFLECT_BEGIN/END
- 使用代码生成器

### 3. 避免 God Object
超过 20 个属性的类通常违反单一职责原则，应当重构

### 4. 文档化 Visitor
使用 BEGIN/END 方案时，确保 visitor 字段与注册字段保持同步

## 常见问题

### Q: 为什么不直接支持无限参数？
**A:** C++ 可变参宏有技术限制，展开开销随参数增加而增大。16 个参数是平衡点。

### Q: 忘记添加 VISIT_FIELD 会怎样？
**A:** 字段仍会注册到反射系统，但无法通过 visitor 访问（序列化等功能受影响）。

### Q: 可以混用两种方案吗？
**A:** 不推荐。选择一种方案并在整个项目中保持一致。

## 相关文件

- [VariadicMacros.h](../Macro/VariadicMacros.h) - 通用可变参宏工具
- [UnifiedReflection.h](./UnifiedReflection.h) - 反射宏定义
- [UnifiedReflectionTest.cpp](./UnifiedReflectionTest.cpp) - 使用示例
- [MetadataSupport.h](./MetadataSupport.h) - 元数据 API

## 示例代码

完整的示例请参考 [UnifiedReflectionTest.cpp](./UnifiedReflectionTest.cpp)
