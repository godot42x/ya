# 统一反射系统

## 🎯 只需要一个宏

现在反射系统已简化为**两个宏**：
- `YA_REFLECT` - 侵入式反射（类内部）
- `YA_REFLECT_EXTERNAL` - 非侵入式反射（类外部，适用于第三方库）

## 使用方式

### 侵入式反射（自定义类）

```cpp
#include "Core/Reflection/UnifiedReflection.h"

struct PlayerComponent {
    YA_REFLECT(PlayerComponent,
        PROP(health, .EditAnywhere().Range(0, 100).Tooltip("Health").Category("Stats")),
        PROP(speed, .EditAnywhere().Range(0, 10).Category("Movement")),
        PROP(cache, .NotSerialized().Transient())
    )
    
    float health = 100.0f;
    float speed = 5.0f;
    float cache = 0.0f;
};

// 使用反射
PlayerComponent player;
player.__visit_properties([](const char* name, const auto& value) {
    // 访问每个属性
    std::cout << name << " = " << value << std::endl;
});
```

### 非侵入式反射（第三方库）

```cpp
// 第三方库的类（无法修改源码）
namespace ThirdParty {
    struct Vector3 {
        float x, y, z;
    };
}

// 在全局作用域注册反射
YA_REFLECT_EXTERNAL(ThirdParty::Vector3,
    PROP(x, .EditAnywhere().Tooltip("X coordinate")),
    PROP(y, .EditAnywhere().Tooltip("Y coordinate")),
    PROP(z, .EditAnywhere().Tooltip("Z coordinate"))
)

// 使用外部反射
ThirdParty::Vector3 vec{1.0f, 2.0f, 3.0f};
ya::reflection::detail::ExternalReflect<ThirdParty::Vector3>::visit_properties(
    vec, [](const char* name, const auto& value) {
        std::cout << name << " = " << value << std::endl;
    }
);
```

## 元数据标记

两种方式共享相同的元数据系统：

| 标记 | 说明 | 示例 |
|------|------|------|
| `.EditAnywhere()` | 可在编辑器中编辑 | `PROP(health, .EditAnywhere())` |
| `.EditReadOnly()` | 只读显示 | `PROP(maxHealth, .EditReadOnly())` |
| `.Range(min, max)` | 值范围限制 | `PROP(health, .Range(0, 100))` |
| `.Tooltip("text")` | 提示信息 | `PROP(speed, .Tooltip("移动速度"))` |
| `.Category("name")` | 分类 | `PROP(health, .Category("Stats"))` |
| `.NotSerialized()` | 不序列化 | `PROP(cache, .NotSerialized())` |
| `.Transient()` | 临时数据 | `PROP(temp, .Transient())` |

## 查询元数据

```cpp
using namespace ya::reflection;

// 使用 reflects-core 的 ClassRegistry 直接访问元数据
auto* cls = ClassRegistry::instance().getClass("PlayerComponent");
if (cls) {
    auto* healthProp = cls->getProperty("health");
    if (healthProp) {
        bool editable = healthProp->flags & PropertyFlags::EditAnywhere;
        float min = healthProp->getMeta<float>("range_min");
        float max = healthProp->getMeta<float>("range_max");
        std::string tooltip = healthProp->getMeta<std::string>("tooltip");
        std::string category = healthProp->getMeta<std::string>("category");
    }
}

// 获取所有可编辑属性
if (cls) {
    for (const auto& [name, prop] : cls->properties) {
        if (prop.flags & PropertyFlags::EditAnywhere) {
            // 处理可编辑属性
        }
    }
}
for (const auto& cat : categories) {
    auto props = registry.getPropertiesByCategory("PlayerComponent", cat);
    // 处理该类别下的属性
}
```

## 文件说明

| 文件 | 说明 | 状态 |
|------|------|------|
| `UnifiedReflection.h` | **统一反射宏** | ✅ **推荐使用** |
| `MetadataSupport.h` | 元数据系统基础设施 | ✅ 核心组件 |
| `UnifiedReflectionTest.cpp` | 完整测试示例 | ✅ 参考代码 |
| `AutoReflect.h` | 旧的纯反射宏 | ⚠️ 已废弃 |
| `ReflectWithMetadata.h` | 旧的分离式宏 | ⚠️ 已废弃 |

## 优势

### vs 旧的分离式宏
❌ **旧方式（两步）**：
```cpp
struct Component {
    YA_REFLECT(Component, health, speed)
    float health, speed;
};

YA_REGISTER_META_BEGIN(Component)
    YA_PROP_META(health, .EditAnywhere())
    YA_PROP_META(speed, .EditAnywhere())
YA_REGISTER_META_END(Component)
```

✅ **新方式（一步）**：
```cpp
struct Component {
    YA_REFLECT(Component,
        PROP(health, .EditAnywhere()),
        PROP(speed, .EditAnywhere())
    )
    float health, speed;
};
```

### 支持非侵入式
```cpp
// 可以为任何第三方库的类添加反射
YA_REFLECT_EXTERNAL(glm::vec3,
    PROP(x, .EditAnywhere()),
    PROP(y, .EditAnywhere()),
    PROP(z, .EditAnywhere())
)
```

## 迁移指南

从旧系统迁移非常简单：

### 从 YA_REFLECT_TYPE 迁移
```cpp
// 旧
struct Component {
    YA_REFLECT_TYPE(Component)
    float health;
};

// 新
struct Component {
    YA_REFLECT(Component,
        PROP(health, )  // 元数据可以为空
    )
    float health;
};
```

### 从分离式宏迁移
```cpp
// 旧
struct Component {
    YA_REFLECT(Component, health, speed)
    float health, speed;
};
YA_REGISTER_META_BEGIN(Component)
    YA_PROP_META(health, .EditAnywhere())
YA_REGISTER_META_END(Component)

// 新
struct Component {
    YA_REFLECT(Component,
        PROP(health, .EditAnywhere()),
        PROP(speed, )  // 可选元数据
    )
    float health, speed;
};
```

## 最佳实践

1. **侵入式优先**：自己的类优先使用 `YA_REFLECT`
2. **非侵入式补充**：第三方库使用 `YA_REFLECT_EXTERNAL`
3. **元数据按需**：不需要元数据的属性可以留空 `PROP(name, )`
4. **统一命名空间**：外部反射时使用完整类型名（包含命名空间）
