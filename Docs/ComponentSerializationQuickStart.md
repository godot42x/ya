# 组件序列化快速指南

## 核心理念

**完全基于反射，无需手写 toJson/fromJson 函数！**

## 添加新组件序列化（3 步）

### 步骤 1: 定义组件

```cpp
// MyComponent.h
struct MyComponent {
    int value = 0;
    float speed = 10.0f;
    glm::vec3 direction = {0.0f, 1.0f, 0.0f};
    bool enabled = true;
};
```

### 步骤 2: 注册反射

```cpp
// MyComponent.h
struct MyComponent {
    // ... 成员变量 ...
    
    static void registerReflection() {
        Register<MyComponent>("MyComponent")
            .constructor<>()
            .property("value", &MyComponent::value)
            .property("speed", &MyComponent::speed)
            .property("direction", &MyComponent::direction)
            .property("enabled", &MyComponent::enabled);
    }
};
```

### 步骤 3: 在启动时注册

```cpp
// ComponentRegistry.cpp
void ComponentRegistry::registerAllComponents() {
    TransformComponent::registerReflection();
    CameraComponent::registerReflection();
    MyComponent::registerReflection();  // 添加这一行
}
```

## 在 SceneSerializer 中使用

```cpp
// SceneSerializer.cpp - serializeEntity()
if (registry.all_of<MyComponent>(handle)) {
    auto& comp = registry.get<MyComponent>(handle);
    j["components"]["MyComponent"] = ReflectionSerializer::serialize(comp);
}

// SceneSerializer.cpp - deserializeEntity()
if (components.contains("MyComponent")) {
    auto comp = ReflectionSerializer::deserialize<MyComponent>(
        components["MyComponent"]);
    registry.emplace<MyComponent>(entity.getHandle(), comp);
}
```

## 支持的类型

### 基础类型
- `int`, `float`, `double`, `bool`
- `std::string`

### glm 数学类型
- `glm::vec2`, `glm::vec3`, `glm::vec4`
- `glm::mat4`

### 添加新类型支持

如果需要支持新类型，在 `ComponentSerializer.h` 中添加：

```cpp
// anyToJson() 中添加
else if (typeHash == typeid(YourType).hash_code()) {
    return yourTypeToJson(std::any_cast<YourType>(value));
}

// jsonToAny() 中添加
else if (typeHash == typeid(YourType).hash_code()) {
    return std::any(yourTypeFromJson(j));
}
```

## 完整示例

```cpp
// 1. 定义组件
struct VelocityComponent {
    glm::vec3 velocity = {0.0f, 0.0f, 0.0f};
    float maxSpeed = 100.0f;
    
    static void registerReflection() {
        Register<VelocityComponent>("VelocityComponent")
            .constructor<>()
            .property("velocity", &VelocityComponent::velocity)
            .property("maxSpeed", &VelocityComponent::maxSpeed);
    }
};

// 2. 注册
VelocityComponent::registerReflection();

// 3. 使用
VelocityComponent vel;
vel.velocity = {10.0f, 0.0f, 5.0f};

// 自动序列化（无需手写代码！）
auto json = ReflectionSerializer::serialize(vel);

// 自动反序列化
auto loaded = ReflectionSerializer::deserialize<VelocityComponent>(json);
```

## 生成的 JSON 格式

```json
{
  "velocity": [10.0, 0.0, 5.0],
  "maxSpeed": 100.0
}
```

## 优势

✅ **零样板代码**: 不需要写 toJson/fromJson  
✅ **类型安全**: 编译期检查  
✅ **易于维护**: 添加字段只需注册一次  
✅ **自动化**: 反射系统自动处理所有细节  
✅ **可扩展**: 轻松添加新类型支持  

## 注意事项

1. **必须先注册**: 在使用序列化前调用 `ComponentRegistry::registerAllComponents()`
2. **公有成员**: 只有公有成员变量可以被反射
3. **支持的类型**: 确保成员类型在 anyToJson/jsonToAny 中有对应处理
4. **构造函数**: 需要注册默认构造函数 `.constructor<>()`

## 对比传统方式

### ❌ 传统方式（不推荐）
```cpp
struct MyComponent {
    int value;
    
    // 需要手写！
    nlohmann::json toJson() const {
        return {{"value", value}};
    }
    
    // 需要手写！
    static MyComponent fromJson(const nlohmann::json& j) {
        return {j["value"].get<int>()};
    }
};
```

### ✅ 反射方式（推荐）
```cpp
struct MyComponent {
    int value;
    
    // 只需注册！
    static void registerReflection() {
        Register<MyComponent>("MyComponent")
            .property("value", &MyComponent::value);
    }
};
```

**节省 50% 以上的代码量！**
