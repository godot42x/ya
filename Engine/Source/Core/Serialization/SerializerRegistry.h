#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace ya
{

/**
 * @brief ECS 组件序列化注册表
 *
 * 为 entt::registry 中的组件提供序列化/反序列化功能
 */
class ECSSerializerRegistry
{
  public:
    using SerializeFunc   = std::function<void(entt::registry &, entt::entity, nlohmann::json &)>;
    using DeserializeFunc = std::function<void(entt::registry &, entt::entity, const nlohmann::json &)>;

    static ECSSerializerRegistry &get();

    void registerSerializer(const std::string &typeName, SerializeFunc serialize, DeserializeFunc deserialize)
    {
        _serializers[typeName]   = std::move(serialize);
        _deserializers[typeName] = std::move(deserialize);
    }

    const auto &getSerializers() const { return _serializers; }
    const auto &getDeserializers() const { return _deserializers; }

  private:
    std::unordered_map<std::string, SerializeFunc>   _serializers;
    std::unordered_map<std::string, DeserializeFunc> _deserializers;
};

} // namespace ya
