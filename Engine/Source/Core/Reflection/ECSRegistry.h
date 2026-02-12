#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "Bus/SceneBus.h"
#include "Core/FName.h"
#include "Core/TypeIndex.h"
#include "reflects-core/lib.h"


namespace ya
{

struct IComponent;
struct ECSRegistry
{

  public:

    static ECSRegistry& get();


    using component_getter  = std::function<void*(const entt::registry&, entt::entity)>;
    using component_creator = std::function<void*(entt::registry&, entt::entity)>;


  private:
    std::unordered_map<FName, uint32_t> _typeIndexCache;
    // TODO: use one function for get,add,remove to reduce memory?
    std::unordered_map<uint32_t, component_getter>  _componentGetters;
    std::unordered_map<uint32_t, component_creator> _componentCreators;
    std::unordered_map<uint32_t, component_creator> _componentRemovers;

  public:

    template <typename T>
    void registerComponent(const std::string& name /*, auto &&componentGetter, auto &&componentCreator*/)
    {
        if constexpr (std::derived_from<T, ::ya::IComponent>) {
            uint32_t typeIndex           = ya::TypeIndex<T>::value();
            _componentGetters[typeIndex] = [](const entt::registry& registry, entt::entity entity) -> void* {
                if (registry.all_of<T>(entity)) {
                    return (void*)&registry.get<T>(entity);
                }
                return nullptr;
            };
            _componentCreators[typeIndex] = [](entt::registry& registry, entt::entity entity) -> void* {
                return (void*)&registry.emplace<T>(entity);
            };
            _componentRemovers[typeIndex] = [](entt::registry& registry, entt::entity entity) -> void* {
                if (registry.all_of<T>(entity)) {
                    registry.remove<T>(entity);
                    SceneBus::get().onComponentRemoved.broadcast(registry, entity, ya::type_index_v<T>);
                    return nullptr;
                }
                return nullptr;
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

    bool hasComponent(ya::type_index_t typeIndex, const entt::registry& registry, entt::entity entity)
    {
        if (auto getterIt = _componentGetters.find(typeIndex); getterIt != _componentGetters.end()) {
            return getterIt->second(registry, entity) != nullptr;
        }
        return false;
    }
    bool hasComponent(FName name, const entt::registry& registry, entt::entity entity)
    {
        // TODO: optimize it for not to  get?
        if (auto typeIndex = getTypeIndex(name)) {
            return hasComponent(typeIndex.value(), registry, entity);
        }
        return false;
    }

    void* getComponent(ya::type_index_t typeIndex, const entt::registry& registry, entt::entity entity)
    {
        if (auto getterIt = _componentGetters.find(typeIndex); getterIt != _componentGetters.end()) {
            return getterIt->second(registry, entity);
        }
        return nullptr;
    }
    void* getComponent(FName name, const entt::registry& registry, entt::entity entity)
    {
        if (auto typeIndex = getTypeIndex(name)) {
            return getComponent(typeIndex.value(), registry, entity);
        }
        return nullptr;
    }

    void* addComponent(ya::type_index_t typeIndex, entt::registry& registry, entt::entity entity)
    {
        if (auto creatorIt = _componentCreators.find(typeIndex); creatorIt != _componentCreators.end()) {
            return creatorIt->second(registry, entity);
        }
        return nullptr;
    }
    void* addComponent(FName name, entt::registry& registry, entt::entity entity)
    {
        if (auto typeIndex = getTypeIndex(name)) {
            return addComponent(typeIndex.value(), registry, entity);
        }
        return nullptr;
    }

    const std::unordered_map<FName, uint32_t>& getTypeIndexCache() const { return _typeIndexCache; }
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
