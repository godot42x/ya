#pragma once

#include <concepts>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

#include "Core/FName.h"
#include "Core/TypeIndex.h"
#include "ECS/ComponentMutation.h"


namespace ya
{

struct IComponent;
struct Scene;

/**
 * @brief How to copy a component during clone/duplicate.
 * - CopyCtor:   Fast. Uses C++ copy constructor directly.
 *               Requires the component to have a correct copy ctor
 *               that resets runtime state (pointers, caches, etc.).
 * - Reflection: Safe. Copies only reflected (serializable) fields via
 *               ReflectionCopier, with serialize→deserialize fallback.
 *               Runtime state stays default-initialized. Slower but
 *               always correct even if copy ctor does shallow copy.
 */
enum class EClonePolicy : uint8_t
{
    CopyCtor,
    Reflection,
};
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

        /**
         * @brief Clone component from srcEntity to dstEntity.
         * @param policy CopyCtor for fast copy, Reflection for safe deep copy.
         * @return Pointer to newly created component, or nullptr if src has no such component.
         */
        virtual void* clone(const entt::registry& srcRegistry, entt::entity srcEntity,
                            entt::registry& dstRegistry, entt::entity dstEntity,
                            EClonePolicy policy) = 0;
    };

    template <typename T>
    struct ComponentOps : public IComponentOps
    {
        void* create(entt::registry& registry, entt::entity entity) override
        {
            return detail_component_mutation::addComponent<T>(registry, entity);
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
            return detail_component_mutation::removeComponent<T>(registry, entity);
        }
        void* clone(const entt::registry& srcRegistry, entt::entity srcEntity,
                     entt::registry& dstRegistry, entt::entity dstEntity,
                     EClonePolicy policy) override;
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
    bool hasComponent(ya::type_index_t typeIndex, const Scene& scene, entt::entity entity);
    bool hasComponent(FName name, const Scene& scene, entt::entity entity);

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
    void* getComponent(ya::type_index_t typeIndex, const Scene& scene, entt::entity entity);
    void* getComponent(FName name, const Scene& scene, entt::entity entity);

    void* addComponent(ya::type_index_t typeIndex, Scene& scene, entt::entity entity);
    void* addComponent(FName name, Scene& scene, entt::entity entity);
    bool removeComponent(ya::type_index_t typeIndex, Scene& scene, entt::entity entity);
    bool removeComponent(FName name, Scene& scene, entt::entity entity);

    /**
     * @brief Clone component from srcEntity to dstEntity.
     * @param policy CopyCtor = fast via copy ctor; Reflection = safe via reflected fields only.
     */
    void* cloneComponent(ya::type_index_t typeIndex,
                         const entt::registry& srcRegistry, entt::entity srcEntity,
                         entt::registry& dstRegistry, entt::entity dstEntity,
                         EClonePolicy policy = EClonePolicy::Reflection)
    {
        if (auto opsIt = _componentOps.find(typeIndex); opsIt != _componentOps.end()) {
            return opsIt->second->clone(srcRegistry, srcEntity, dstRegistry, dstEntity, policy);
        }
        return nullptr;
    }

    [[nodiscard]] const std::unordered_map<FName, uint32_t>& getTypeIndexCache() const { return _typeIndexCache; }
};

} // namespace ya

// ============================================================================
// ComponentOps<T>::clone — defined after ECSRegistry to keep the class body clean.
// Needs ReflectionCopier / ReflectionSerializer only for the Reflection path.
// ============================================================================
#include "Core/Reflection/ReflectionSerializer.h"
#include "Core/Reflection/ReflectionCopier.h"
#include "reflects-core/lib.h"

namespace ya
{

template <typename T>
void* ECSRegistry::ComponentOps<T>::clone(
    const entt::registry& srcRegistry, entt::entity srcEntity,
    entt::registry& dstRegistry, entt::entity dstEntity,
    EClonePolicy policy)
{
    if (!srcRegistry.all_of<T>(srcEntity)) {
        return nullptr;
    }

    if (policy == EClonePolicy::CopyCtor) {
        // Fast path: C++ copy constructor (caller must ensure T's copy ctor is correct)
        const T& src = srcRegistry.get<T>(srcEntity);
        return &dstRegistry.emplace_or_replace<T>(dstEntity, src);
    }

    // Safe path: default-construct, then copy only reflected fields
    T&          dst = dstRegistry.emplace_or_replace<T>(dstEntity);
    const T&    src = srcRegistry.get<T>(srcEntity);
    uint32_t    typeIndex = ya::TypeIndex<T>::value();
    const auto* cls = ClassRegistry::instance().getClass(typeIndex);

    if (cls) {
        bool copied = ReflectionCopier::copyByRuntimeReflection(
            &dst, &src, typeIndex, cls->getName());

        if (!copied) {
            // Fallback: serialize → deserialize
            auto json = ReflectionSerializer::serializeByRuntimeReflection(
                &src, typeIndex, cls->getName());
            ReflectionSerializer::deserializeByRuntimeReflection(
                &dst, typeIndex, json, cls->getName());
        }
    }

    return &dst;
}

} // namespace ya
