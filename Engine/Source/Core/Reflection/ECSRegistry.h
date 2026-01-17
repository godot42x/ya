#pragma once

#include "Core/FName.h"
#include "Core/TypeIndex.h"
#include <entt/entt.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <reflects-core/lib.h>
#include <string>
#include <unordered_map>

namespace ya
{

struct IComponent;
struct ECSRegistry
{

  public:

    static ECSRegistry &get();


    using component_getter  = std::function<void *(entt::registry &, entt::entity)>;
    using component_creator = std::function<void *(entt::registry &, entt::entity)>;

    std::unordered_map<FName, uint32_t>             _typeIndexCache;
    std::unordered_map<uint32_t, component_getter>  _componentGetters;
    std::unordered_map<uint32_t, component_creator> _componentCreators;

    template <typename T>
    void registerComponent(const std::string &name /*, auto &&componentGetter, auto &&componentCreator*/)
    {
        if constexpr (std::derived_from<T, ::ya::IComponent>) {
            uint32_t typeIndex           = ya::TypeIndex<T>::value();
            _componentGetters[typeIndex] = [](entt::registry &registry, entt::entity entity) -> void * {
                if (registry.all_of<T>(entity)) {
                    return &registry.get<T>(entity);
                }
            };
            _componentCreators[typeIndex] = [](entt::registry &registry, entt::entity entity) -> void * {
                return &registry.emplace<T>(entity);
            };
            _typeIndexCache[FName(name)] = typeIndex;
        }
    }
    std::optional<type_index_t> getTypeIndex(FName name)
    {
        auto typeIndexIt = _typeIndexCache.find(name);
        if (typeIndexIt != _typeIndexCache.end()) {
            return typeIndexIt->second;
        }
        return {};
    }

    bool hasType(FName name)
    {
        return _typeIndexCache.find(name) != _typeIndexCache.end();
    }

    auto getComponent(FName name, entt::registry &registry, entt::entity entity) -> void *
    {
        auto typeIndexIt = _typeIndexCache.find(name);
        if (typeIndexIt != _typeIndexCache.end()) {
            if (auto getterIt = _componentGetters.find(typeIndexIt->second); getterIt != _componentGetters.end()) {
                return getterIt->second(registry, entity);
            }
        }
        return nullptr;
    }
    auto addComponent(FName name, entt::registry &registry, entt::entity entity) -> void *
    {
        auto typeIndexIt = _typeIndexCache.find(name);
        if (typeIndexIt != _typeIndexCache.end()) {
            if (auto creatorIt = _componentCreators.find(typeIndexIt->second); creatorIt != _componentCreators.end()) {
                return creatorIt->second(registry, entity);
            }
        }
        return nullptr;
    }
};


#if 1
namespace
{
void test()
{
    ECSRegistry::get().registerComponent<ECSRegistry>("TestComponent");
}
} // namespace
#endif

} // namespace ya
