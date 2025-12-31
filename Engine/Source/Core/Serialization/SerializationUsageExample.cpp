#include "Core/App/App.h"
#include "Core/Log.h"
#include "Core/System//ReflectionSystem.h"
#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Scene/SceneManager.h"
#include "SceneSerializer.h"



/**
 * @file SerializationUsageExample.cpp
 * @brief Scene 序列化使用示例
 */

namespace ya
{

// ============================================================================
// 示例 1: 保存和加载场景
// ============================================================================

void Example_SaveAndLoadScene()
{
    // 1. 在应用启动时注册所有组件的反射信息（只需调用一次）
    ReflectionSystem::registerAllComponents();

    // 2. 创建场景
    Scene scene("MyTestScene");

    // 3. 创建一些实体
    auto *player          = scene.createEntity("Player");
    auto &playerTransform = scene.getRegistry().emplace<TransformComponent>(player->getHandle());
    playerTransform.setPosition({1.0f, 2.0f, 3.0f});
    playerTransform.setRotation({0.0f, 45.0f, 0.0f});
    playerTransform.setScale({1.0f, 1.0f, 1.0f});

    auto *camera          = scene.createEntity("MainCamera");
    auto &cameraTransform = scene.getRegistry().emplace<TransformComponent>(camera->getHandle());
    cameraTransform.setPosition({0.0f, 5.0f, -10.0f});

    auto &cameraComp        = scene.getRegistry().emplace<CameraComponent>(camera->getHandle());
    cameraComp._fov         = 60.0f;
    cameraComp._aspectRatio = 16.0f / 9.0f;
    cameraComp._nearClip    = 0.1f;
    cameraComp._farClip     = 1000.0f;

    // 4. 保存场景
    SceneSerializer serializer(&scene);
    bool            success = serializer.saveToFile("Content/TestScene.json");

    if (success) {
        YA_CORE_INFO("Scene saved successfully!");
    }

    // 5. 加载场景
    Scene           loadedScene("LoadedScene");
    SceneSerializer loader(&loadedScene);
    success = loader.loadFromFile("Content/TestScene.json");

    if (success) {
        YA_CORE_INFO("Scene loaded successfully!");
        YA_CORE_INFO("Scene name: {}", loadedScene.getName());
    }
}

// ============================================================================
// 示例 2: 使用反射自动序列化组件（无需手写 toJson/fromJson）
// ============================================================================

void Example_ReflectionComponentSerialization()
{
    // 1. 注册反射信息（只需一次，通常在启动时）
    ReflectionSystem::registerAllComponents();

    // 2. 创建组件
    TransformComponent transform;
    transform._position = {10.0f, 20.0f, 30.0f};
    transform._rotation = {0.0f, 90.0f, 0.0f};
    transform._scale    = {2.0f, 2.0f, 2.0f};

    // 3. 使用反射自动序列化（无需手写 toJson）
    nlohmann::json j = ReflectionSerializer::serialize(transform);

    // 4. 打印 JSON
    YA_CORE_INFO("Serialized Transform:\n{}", j.dump(4));

    // 5. 使用反射自动反序列化（无需手写 fromJson）
    TransformComponent loaded = ReflectionSerializer::deserialize<TransformComponent>(j);

    YA_CORE_INFO("Loaded position: ({}, {}, {})",
                 loaded._position.x,
                 loaded._position.y,
                 loaded._position.z);
}

// ============================================================================
// 示例 3: 添加新组件的序列化（只需注册反射）
// ============================================================================

// 步骤 1: 定义组件
struct HealthComponent
{
    int   health    = 100;
    int   maxHealth = 100;
    float regenRate = 5.0f;
    bool  isDead    = false;

    // 步骤 2: 注册反射（不需要写 toJson/fromJson！）
    static void registerReflection()
    {
        Register<HealthComponent>("HealthComponent")
            .constructor<>()
            .property("health", &HealthComponent::health)
            .property("maxHealth", &HealthComponent::maxHealth)
            .property("regenRate", &HealthComponent::regenRate)
            .property("isDead", &HealthComponent::isDead);
    }
};

void Example_NewComponentSerialization()
{
    // 注册
    HealthComponent::registerReflection();

    // 创建
    HealthComponent health;
    health.health    = 75;
    health.maxHealth = 100;

    // 自动序列化
    auto json = ReflectionSerializer::serialize(health);
    YA_CORE_INFO("Health component:\n{}", json.dump(4));

    // 自动反序列化
    auto loaded = ReflectionSerializer::deserialize<HealthComponent>(json);
    YA_CORE_INFO("Loaded health: {}/{}", loaded.health, loaded.maxHealth);
}

// ============================================================================
// 示例 4: 在 SceneSerializer 中添加新组件支持
// ============================================================================

/*
在 SceneSerializer.cpp 中添加新组件非常简单：

// 序列化
if (registry.all_of<HealthComponent>(handle)) {
    auto& health = registry.get<HealthComponent>(handle);
    j["components"]["HealthComponent"] = ReflectionSerializer::serialize(health);
}

// 反序列化
if (components.contains("HealthComponent")) {
    auto health = ReflectionSerializer::deserialize<HealthComponent>(
        components["HealthComponent"]);
    registry.emplace<HealthComponent>(entity.getHandle(), health);
}

就这么简单！无需写任何 toJson/fromJson 代码！
*/

// ============================================================================
// 示例 5: 在游戏中集成
// ============================================================================

class MyGame : public App
{
    void onInit(AppDesc ci) override
    {
        App::onInit(ci);

        // 注册所有组件的反射信息
        ReflectionSystem::registerAllComponents();

        // 设置游戏根目录
        // auto gameRoot = FileSystem::get()->getProjectRoot() / "Example" / "MyGame";
        // FileSystem::get()->setGameRoot(gameRoot);

        // 加载场景
        auto           *scene = getSceneManager()->getEditorScene();
        SceneSerializer serializer(scene);

        if (serializer.loadFromFile("Content/MainScene.json")) {
            YA_CORE_INFO("Main scene loaded successfully");
        }
        else {
            YA_CORE_WARN("Failed to load main scene, using empty scene");
        }
    }

    void onQuit() override
    {
        // 可选：保存场景
        auto           *scene = getSceneManager()->getEditorScene();
        SceneSerializer serializer(scene);
        serializer.saveToFile("Content/MainScene_Autosave.json");

        App::onQuit();
    }
};

} // namespace ya
