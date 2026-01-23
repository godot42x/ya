#pragma once

#include <Scene/Scene.h>
#include <fstream>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <type_traits>



namespace ya
{


/**
 * @brief Scene 序列化器 - 基于 ECS + 反射
 *
 * 设计目标:
 * 1. 自动序列化所有注册的 Component
 * 2. 支持手动序列化（toJson/fromJson）和反射序列化
 * 3. 支持 Entity 层级结构（Scene Graph）
 * 4. 支持资源引用（Texture, Mesh 等）
 */

struct SceneSerializer
{
  private:
    Scene *_scene = nullptr;


  public:
    SceneSerializer(Scene *scene) : _scene(scene) {}

    bool saveToFile(const std::string &filepath);
    bool loadFromFile(const std::string &filepath);

    nlohmann::json serialize();
    void           deserialize(const nlohmann::json &j);


    nlohmann::json serializeEntity(Entity *entity);
    Entity        *deserializeEntity(const nlohmann::json &j);

    // NodeTree 序列化 - 树状结构（只存引用）
    nlohmann::json serializeNodeTree(Node *node);
    void           deserializeNodeTree(const nlohmann::json &j, Node *parent, 
                                       const std::unordered_map<uint64_t, Entity*> &entityMap);

    // Component 序列化 - 通过反射自动处理
    template <typename ComponentType>
    nlohmann::json serializeComponent(const ComponentType &component);
    template <typename ComponentType>
    void deserializeComponent(ComponentType &component, const nlohmann::json &j);
};



} // namespace ya
