#pragma once

#include <concepts>
#include <entt/entt.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include "Bus/SceneBus.h"
#include "Core/FName.h"
#include "Core/TypeIndex.h"


namespace ya
{

struct IComponent;
struct ECSRegistry
{

  public:

    static ECSRegistry& get();



    // TODO: optimize and profile memory
    // method 1: store 1 object
    struct IComponentOps
    {
        virtual ~IComponentOps()                                               = default;
        virtual void* create(entt::registry& registry, entt::entity entity)    = 0;
        virtual void* get(const entt::registry& registry, entt::entity entity) = 0;
        virtual bool  remove(entt::registry& registry, entt::entity entity)    = 0;
    };

    template <typename T>
    struct ComponentOps : public IComponentOps
    {
        void* create(entt::registry& registry, entt::entity entity) override
        {
            return &registry.emplace<T>(entity);
        }
        void* get(const entt::registry& registry, entt::entity entity) override
        {
            if (registry.all_of<T>(entity)) {
                return const_cast<T*>(&registry.get<T>(entity));
            }
            return nullptr;
        }
        bool remove(entt::registry& registry, entt::entity entity) override
        {
            if (registry.all_of<T>(entity)) {
                registry.remove<T>(entity);
                SceneBus::get().onComponentRemoved.broadcast(registry, entity, ya::type_index_v<T>);
                return true;
            }
            return false;
        }
    };

  private:
    std::unordered_map<FName, uint32_t>          _typeIndexCache;
    std::unordered_map<uint32_t, IComponentOps*> _componentOps;

  public:

    template <typename T>
    void registerComponent(const std::string& name /*, auto &&componentGetter, auto &&componentCreator*/)
    {
        if constexpr (std::derived_from<T, ::ya::IComponent>) {
            const uint32_t typeIndex       = ya::TypeIndex<T>::value();
            IComponentOps* componentOpsPtr = new ComponentOps<T>{};
            _componentOps[typeIndex]       = componentOpsPtr;
            _typeIndexCache[FName(name)]   = typeIndex;
        }
    }

    ~ECSRegistry()
    {
        for (auto& [_, opsPtr] : _componentOps) {
            delete opsPtr;
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
        if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
            return opsIt->second->get(registry, entity) != nullptr;
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
        if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
            return opsIt->second->get(registry, entity);
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
        if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
            return opsIt->second->create(registry, entity);
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

} // namespace ya
