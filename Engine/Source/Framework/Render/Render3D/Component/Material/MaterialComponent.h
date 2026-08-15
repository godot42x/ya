#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "ECS/Component/Material/MaterialComponentBase.h"


namespace ya
{

enum class EMaterialResolveState : uint8_t
{
    Dirty = 0,
    Resolving,
    Ready,
    Failed,
};

enum class EMaterialResolveResult : uint8_t
{
    Pending = 0,
    Ready,
    Failed,
};

/**
 * @brief Template material component base
 *
 * Provides type-safe material access and common functionality.
 * Does NOT handle mesh data - use StaticMeshComponent/SkinnedMeshComponent for
 * that.
 *
 * The runtime material instance is stored opaquely in MaterialComponentBase
 * and destroyed through the render module's MaterialFactory; this header stays
 * free of Render3D types (the concrete material type is only forward-declared
 * and completed at instantiation sites).
 *
 * @tparam MaterialType The concrete material type (e.g., PhongMaterial)
 */
template <typename MaterialType>
struct MaterialComponent : public MaterialComponentBase
{
    using material_t = MaterialType;
    YA_REFLECT_BEGIN(MaterialComponent<MaterialType>, IComponent)
    YA_REFLECT_FIELD(_materialPath)
    YA_REFLECT_END()

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    std::string _materialPath;

  public:
    MaterialComponent()
    {
        MaterialComponent<MaterialType>::__ensure_reflection_registered();
    }

    virtual ~MaterialComponent() = default; // MaterialComponentBase destroys via MaterialFactory

    /**
     * @brief Resolve all resources (textures, etc.)
     * Called by GameplayResourceBinding
     * Derived classes should override this
     */
    virtual EMaterialResolveResult resolve() { return EMaterialResolveResult::Ready; }

    /**
     * @brief Force re-resolve (invalidate cache)
     */
    void invalidate()
    {
        releaseMaterial();
    }

    /**
     * @brief Set a shared material (will not be destroyed by this component)
     */
    void setSharedMaterial(MaterialType* material);

    [[nodiscard]] MaterialType* getMaterial() const;

    void setMaterial(MaterialType* material);

    [[nodiscard]] bool isUsingSharedMaterial() const { return _material != nullptr && _bSharedMaterial; }
};

// Out-of-line template definitions. The concrete material type (MaterialType)
// is deliberately incomplete in this header, so the pointer conversions go
// through `void*`. This is a standard conversion chain and yields the same
// address: Material is the root base of every MaterialType (single, non-virtual
// inheritance), so the Material subobject starts at offset zero.
template <typename MaterialType>
void MaterialComponent<MaterialType>::setSharedMaterial(MaterialType* material)
{
    setSharedMaterialBase(static_cast<Material*>(static_cast<void*>(material)));
}

template <typename MaterialType>
MaterialType* MaterialComponent<MaterialType>::getMaterial() const
{
    return static_cast<MaterialType*>(static_cast<void*>(_material));
}

template <typename MaterialType>
void MaterialComponent<MaterialType>::setMaterial(MaterialType* material)
{
    setSharedMaterial(material);
}


} // namespace ya
