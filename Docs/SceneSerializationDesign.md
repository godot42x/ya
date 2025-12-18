# Scene 序列化框架设计文档

## 架构概览

```
┌─────────────────────────────────────────────────┐
│           SceneSerializer (核心)                  │
│  ┌──────────────┐      ┌────────────────────┐  │
│  │ Scene        │◄─────┤ JSON 序列化器       │  │
│  │ - entities   │      │ - toJson()          │  │
│  │ - metadata   │      │ - fromJson()        │  │
│  └──────────────┘      └────────────────────┘  │
└─────────────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
┌───────▼────────┐    ┌────────▼─────────┐
│ Entity         │    │ Component        │
│ 序列化器        │    │ 序列化器          │
│ - ID/Name      │    │ - 手动实现        │
│ - Components   │    │ - 反射自动化      │
└────────────────┘    └──────────────────┘
```

## 设计原则

1. **分层序列化**: Scene → Entity → Component
2. **双模式支持**: 手动实现 toJson() 或 反射自动化
3. **类型安全**: 使用 nlohmann::json 和 C++ 模板
4. **可扩展**: 易于添加新组件类型

## 核心类

### 1. SceneSerializer

**职责**: 管理整个 Scene 的序列化/反序列化

```cpp
class SceneSerializer {
    Scene* _scene;
    
    // 保存/加载文件
    bool saveToFile(const std::string& filepath);
    bool loadFromFile(const std::string& filepath);
    
    // JSON 序列化
    nlohmann::json serialize();
    void deserialize(const nlohmann::json& j);
    
private:
    nlohmann::json serializeEntity(Entity* entity);
    Entity* deserializeEntity(const nlohmann::json& j);
};
```

**使用示例:**
```cpp
// 保存场景
SceneSerializer serializer(scene);
serializer.saveToFile("Content/Scene.json");

// 加载场景
SceneSerializer serializer(emptyScene);
serializer.loadFromFile("Content/Scene.json");
```

### 2. ReflectionSerializer

**职责**: 通过反射系统自动序列化任意组件

```cpp
struct ReflectionSerializer {
    template<typename T>
    static nlohmann::json serialize(const T& obj);
    
    template<typename T>
    static T deserialize(const nlohmann::json& j);
};
```

**使用示例:**
```cpp
TransformComponent transform;
transform._position = {1.0f, 2.0f, 3.0f};

// 自动序列化
auto json = ReflectionSerializer::serialize(transform);

// 自动反序列化
auto loadedTransform = ReflectionSerializer::deserialize<TransformComponent>(json);
```

## 组件序列化方式

### 方式 1: 手动实现 (推荐用于复杂组件)

```cpp
struct TransformComponent {
    glm::vec3 _position;
    glm::vec3 _rotation;
    glm::vec3 _scale;
    
    nlohmann::json toJson() const {
        return {
            {"position", SerializerHelper::toJson(_position)},
            {"rotation", SerializerHelper::toJson(_rotation)},
            {"scale", SerializerHelper::toJson(_scale)}
        };
    }
    
    static TransformComponent fromJson(const nlohmann::json& j) {
        TransformComponent tc;
        tc._position = SerializerHelper::toVec3(j["position"]);
        tc._rotation = SerializerHelper::toVec3(j["rotation"]);
        tc._scale = SerializerHelper::toVec3(j["scale"]);
        return tc;
    }
};
```

### 方式 2: 反射自动化 (推荐用于简单组件)

```cpp
struct HealthComponent {
    int health = 100;
    int maxHealth = 100;
    
    // 注册反射信息
    static void registerReflection() {
        Register<HealthComponent>("HealthComponent")
            .property("health", &HealthComponent::health)
            .property("maxHealth", &HealthComponent::maxHealth);
    }
};

// 在初始化时注册
HealthComponent::registerReflection();

// 自动序列化
auto json = ReflectionSerializer::serialize(healthComp);
```

### 方式 3: 使用宏 (未来实现)

```cpp
struct VelocityComponent {
    glm::vec3 velocity;
    float maxSpeed = 10.0f;
    
    YA_COMPONENT_SERIALIZABLE()
    YA_PROPERTY(velocity)
    YA_PROPERTY(maxSpeed)
};
```

## JSON 格式示例

### Scene 文件结构

```json
{
  "version": "1.0",
  "name": "MyScene",
  "entities": [
    {
      "id": 1,
      "name": "Player",
      "components": {
        "TransformComponent": {
          "position": [0.0, 0.0, 0.0],
          "rotation": [0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "CameraComponent": {
          "fov": 60.0,
          "aspectRatio": 1.777,
          "nearPlane": 0.1,
          "farPlane": 1000.0,
          "isActive": true
        }
      }
    },
    {
      "id": 2,
      "name": "Enemy",
      "components": {
        "TransformComponent": {
          "position": [10.0, 0.0, 5.0],
          "rotation": [0.0, 90.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "HealthComponent": {
          "health": 100,
          "maxHealth": 100
        }
      }
    }
  ]
}
```

## 实现计划

### Phase 1: 基础框架 ✅
- [x] SceneSerializer 基本结构
- [x] 手动序列化 TransformComponent
- [x] 手动序列化 CameraComponent
- [x] 文件保存/加载

### Phase 2: 反射集成 (进行中)
- [ ] ReflectionSerializer 实现
- [ ] anyToJson / jsonToAny 类型转换
- [ ] 为所有组件注册反射信息
- [ ] 自动序列化测试

### Phase 3: 高级特性
- [ ] 资源引用序列化 (Texture, Mesh, Material)
- [ ] Scene Graph 层级序列化
- [ ] Prefab 支持
- [ ] 版本兼容性处理

### Phase 4: 编辑器集成
- [ ] 场景编辑器 UI
- [ ] 拖拽保存/加载
- [ ] 实时预览
- [ ] 撤销/重做支持

## 使用指南

### 在游戏中使用

```cpp
// GreedySnake.h
struct GreedySnake : public ya::App {
    void onInit(ya::AppDesc ci) override {
        Super::onInit(ci);
        
        // 加载场景
        auto scene = getSceneManager()->getActiveScene();
        SceneSerializer serializer(scene);
        serializer.loadFromFile("Content/Scene.json");
    }
    
    void onQuit() override {
        // 保存场景（可选）
        auto scene = getSceneManager()->getActiveScene();
        SceneSerializer serializer(scene);
        serializer.saveToFile("Content/Scene_Autosave.json");
        
        Super::onQuit();
    }
};
```

### 添加新组件序列化

**步骤 1**: 为组件实现 toJson/fromJson

```cpp
// MyComponent.h
struct MyComponent {
    int value;
    std::string name;
    
    nlohmann::json toJson() const {
        return {
            {"value", value},
            {"name", name}
        };
    }
    
    static MyComponent fromJson(const nlohmann::json& j) {
        return {
            j["value"].get<int>(),
            j["name"].get<std::string>()
        };
    }
};
```

**步骤 2**: 在 SceneSerializer 中注册

```cpp
// SceneSerializer.cpp
nlohmann::json SceneSerializer::serializeEntity(Entity* entity) {
    // ...
    
    // MyComponent
    if (registry.all_of<MyComponent>(handle)) {
        auto& comp = registry.get<MyComponent>(handle);
        j["components"]["MyComponent"] = comp.toJson();
    }
    
    return j;
}

Entity* SceneSerializer::deserializeEntity(const nlohmann::json& j) {
    // ...
    
    // MyComponent
    if (components.contains("MyComponent")) {
        auto comp = MyComponent::fromJson(components["MyComponent"]);
        registry.emplace<MyComponent>(entity.getHandle(), comp);
    }
    
    return entity;
}
```

## 最佳实践

1. **简单组件用反射**: 减少样板代码
2. **复杂组件手动实现**: 更好的控制和优化
3. **版本字段必须**: 便于未来兼容性
4. **资源用路径不用指针**: 序列化为 "Content/Textures/player.png"
5. **测试反序列化**: 确保加载的数据正确

## 未来优化

- **增量保存**: 只序列化修改的 Entity
- **二进制格式**: 运行时使用更快的二进制格式
- **压缩**: 减小文件大小
- **加密**: 保护游戏数据
- **网络同步**: 支持多人游戏场景同步
