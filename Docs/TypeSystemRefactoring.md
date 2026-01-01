# 类型系统重构：TypeRegistry 统一管理

## 问题分析

### 重构前的问题

1. **硬编码类型转换**：
   - `LuaScriptingSystem::anyToLuaObject()` - 30+ 行 if-else
   - `LuaScriptingSystem::luaObjectToAny()` - 40+ 行 if-else
   - `SerializerHelper::anyToJson()` - 50+ 行 if-else
   - `SerializerHelper::jsonToAny()` - 50+ 行 if-else
   - `LuaScriptComponent::inferType()` - 25+ 行 switch-case
   - `LuaScriptComponent::applyPropertyOverrides()` - 60+ 行 if-else

2. **重复逻辑**：每次新增类型（如 `Vec4`, `Mat4`）需要修改 **6+ 处代码**

3. **耦合严重**：类型判断分散在 Lua、序列化、组件等多个模块

4. **维护困难**：类型名字符串（"float", "Vec3"）硬编码，容易拼写错误

## 重构方案

### 核心设计：TypeRegistry

创建统一的类型注册表，每个类型注册一次，自动生成所有转换函数。

```cpp
// 注册一个类型
TypeRegistry::get()->registerType<float>("float")
    .anyToLua([](const std::any& v, sol::state_view lua) { 
        return sol::make_object(lua, std::any_cast<float>(v)); 
    })
    .luaToAny([](const sol::object& obj, std::any& out) {
        if (obj.is<double>()) { out = obj.as<float>(); return true; }
        return false;
    })
    .anyToJson([](const std::any& v) { return std::any_cast<float>(v); })
    .jsonToAny([](const nlohmann::json& j) { return std::any(j.get<float>()); })
    .stringToAny([](const std::string& s) { return std::any(std::stof(s)); })
    .luaTypeChecker([](const sol::object& obj) { return obj.is<double>(); });
```

### 架构优势

1. **单一注册点**：新增类型只需修改 `TypeRegistry.cpp` 一处
2. **自动推断**：`inferTypeFromLua()` 遍历注册表，通过 checker 匹配
3. **统一接口**：所有模块调用同一套 API
4. **类型安全**：通过 `refl::type_index_v<T>` 绑定编译期类型

## 重构内容

### 新增文件

1. **`Core/System/TypeRegistry.h`** (250+ 行)
   - `TypeRegistry` 单例类
   - `TypeInfo` 结构体（包含所有转换函数）
   - `TypeBuilder` 流式注册接口

2. **`Core/System/TypeRegistry.cpp`** (290+ 行)
   - `TypeRegistryInitializer` 静态初始化器
   - 注册 10 种基础类型：`int`, `float`, `double`, `bool`, `string`, `Vec2`, `Vec3`, `Vec4`, `Mat4`

### 修改文件

1. **`LuaScriptingSystem.h/cpp`**
   - 删除 `anyToLuaObject()` / `luaObjectToAny()` 实现（-70 行）
   - 替换为 `TypeRegistry::get()->anyToLuaObject()` 调用
   - 4 处调用点（__index, __newindex, getter, setter）

2. **`LuaScriptComponent.cpp`**
   - `inferType()` 改为调用 `TypeRegistry::get()->inferTypeFromLua()` (-20 行)
   - `applyPropertyOverrides()` 使用 `TypeRegistry::get()->stringToAny()` (-50 行)

3. **`SerializerHelper.h`**
   - 删除 `toJson(glm::vec3)` 等工具函数（-120 行）
   - `anyToJson()` / `jsonToAny()` 改为委托 TypeRegistry（-100 行）

### 代码量统计

| 模块 | 重构前 | 重构后 | 变化 |
|------|--------|--------|------|
| LuaScriptingSystem | ~70 行硬编码 | 调用 TypeRegistry | -70 行 |
| LuaScriptComponent | ~75 行硬编码 | 调用 TypeRegistry | -75 行 |
| SerializerHelper | ~120 行硬编码 | 调用 TypeRegistry | -120 行 |
| **总计删除** | - | - | **-265 行重复代码** |
| TypeRegistry | 0 | 540 行（可扩展） | +540 行 |
| **净增加** | - | - | **+275 行（一次性投入）** |

## 新增类型示例

### 重构前（需修改 6 处）

```cpp
// 1. LuaScriptingSystem::anyToLuaObject
else if (typeIndex == refl::TypeIndex<Color>::value()) {
    return sol::make_object(lua, std::any_cast<Color>(value));
}

// 2. LuaScriptingSystem::luaObjectToAny
else if (typeIndex == refl::TypeIndex<Color>::value()) {
    if (luaValue.is<Color>()) { outValue = luaValue.as<Color>(); return true; }
}

// 3. SerializerHelper::anyToJson
else if (typeIndex == type_index_v<Color>) {
    auto c = std::any_cast<Color>(value);
    return {c.r, c.g, c.b, c.a};
}

// 4. SerializerHelper::jsonToAny
else if (typeHash == typeid(Color).hash_code()) {
    return Color{j[0], j[1], j[2], j[3]};
}

// 5. LuaScriptComponent::inferType
if (value.is<Color>()) return "Color";

// 6. LuaScriptComponent::applyPropertyOverrides
else if (typeHint == "Color") {
    float r, g, b, a;
    sscanf(serializedValue.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
    self[propName] = Color{r, g, b, a};
}
```

### 重构后（只需修改 1 处）

```cpp
// TypeRegistry.cpp - 一处注册即可
registry->registerType<Color>("Color")
    .anyToLua([](const std::any& v, sol::state_view lua) {
        return sol::make_object(lua, std::any_cast<Color>(v));
    })
    .luaToAny([](const sol::object& obj, std::any& out) {
        if (obj.is<Color>()) { out = obj.as<Color>(); return true; }
        return false;
    })
    .anyToJson([](const std::any& v) {
        auto c = std::any_cast<Color>(v);
        return nlohmann::json::array({c.r, c.g, c.b, c.a});
    })
    .jsonToAny([](const nlohmann::json& j) {
        return Color{j[0], j[1], j[2], j[3]};
    })
    .stringToAny([](const std::string& s) {
        float r, g, b, a;
        sscanf(s.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
        return Color{r, g, b, a};
    })
    .luaTypeChecker([](const sol::object& obj) {
        return obj.is<Color>();
    });
```

## 性能影响

- **编译期**：类型注册在静态初始化阶段完成，零运行时开销
- **运行期**：从 `if-else` 链改为 `unordered_map` 查找
  - 旧方案：O(n) 线性查找（n = 类型数量）
  - 新方案：O(1) 哈希查找
  - **性能提升**：类型多时更快（10+ 类型场景）

## 后续优化建议

1. **自动注册宏**：
   ```cpp
   REGISTER_TYPE(Color, "Color")
       .autoGenerateConverters() // 自动生成标准转换
       .customSerializer([](Color c) { return json{c.r, c.g, c.b, c.a}; });
   ```

2. **类型反射集成**：
   - 直接从 `refl::Class` 读取类型信息
   - 自动生成 Lua 绑定和序列化代码

3. **错误处理**：
   - 添加类型转换失败的详细日志
   - 支持自定义错误回调

## 测试验证

- ✅ 编译通过：无警告/错误
- ✅ HelloMaterial 运行正常
- ✅ Lua 脚本属性推断正确
- ✅ 场景序列化/反序列化成功

## 总结

通过引入 `TypeRegistry`，将分散在 6 处的类型硬编码集中管理，删除 265 行重复代码，新增类型从修改 6 处降为 1 处，显著提升可维护性和扩展性。
