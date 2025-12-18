#pragma once

#include <Scene/Scene.h>
#include <fstream>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <type_traits>



namespace ya
{

// Component 序列化特征
template <typename T>
concept Serializable = requires(T t, nlohmann::json j) {
    {
        t.toJson()
    } -> std::same_as<nlohmann::json>;
    {
        T::fromJson(j)
    } -> std::same_as<T>;
};

/**
 * @brief Scene 序列化器 - 基于 ECS + 反射
 *
 * 设计目标:
 * 1. 自动序列化所有注册的 Component
 * 2. 支持手动序列化（toJson/fromJson）和反射序列化
 * 3. 支持 Entity 层级结构（Scene Graph）
 * 4. 支持资源引用（Texture, Mesh 等）
 */

using ComponentSerializer   = std::function<nlohmann::json(entt::registry &, entt::entity)>;
using ComponentDeserializer = std::function<void(entt::registry &, entt::entity, const nlohmann::json &)>;
struct SceneSerializer
{
    // 特殊组件序列化器注册表

    static std::unordered_map<std::string, ComponentSerializer>   _componentSerializers;
    static std::unordered_map<std::string, ComponentDeserializer> _componentDeserializers;

  public:
    SceneSerializer(Scene *scene) : _scene(scene) {}


    static void registerComponentSerializers();

    // 保存场景到文件
    bool saveToFile(const std::string &filepath);

    // 从文件加载场景
    bool loadFromFile(const std::string &filepath);

    // 序列化为 JSON
    nlohmann::json serialize();

    // 从 JSON 反序列化
    void deserialize(const nlohmann::json &j);

  private:
    Scene *_scene = nullptr;

    // Entity 序列化
    nlohmann::json serializeEntity(Entity *entity);
    Entity        *deserializeEntity(const nlohmann::json &j);

    // Component 序列化 - 通过反射自动处理
    template <typename ComponentType>
    nlohmann::json serializeComponent(const ComponentType &component);

    template <typename ComponentType>
    void deserializeComponent(ComponentType &component, const nlohmann::json &j);


    // 注册组件序列化器

    // 通过组件类型名查找序列化器
    nlohmann::json serializeComponentByName(const std::string &typeName,
                                            entt::registry    &registry,
                                            entt::entity       entity);
};

// ============================================================================
// Component 自动序列化宏
// ============================================================================

/**
 * 在 Component 中使用此宏自动生成序列化方法
 *
 * 示例:
 * struct TransformComponent {
 *     glm::vec3 position;
 *     glm::vec3 rotation;
 *     glm::vec3 scale;
 *
 *     COMPONENT_SERIALIZABLE(TransformComponent)
 *     COMPONENT_PROPERTY(position)
 *     COMPONENT_PROPERTY(rotation)
 *     COMPONENT_PROPERTY(scale)
 * };
 */
#define COMPONENT_SERIALIZABLE(ComponentType) \
    nlohmann::json       toJson() const;      \
    static ComponentType fromJson(const nlohmann::json &j);

#define COMPONENT_PROPERTY(propName) \
    // 在实现中通过反射自动处理


} // namespace ya
