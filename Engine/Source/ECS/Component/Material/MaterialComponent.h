#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Material/Material.h"


namespace ya
{


/**
 * @brief Template material component base
 *
 * Provides type-safe material access and common functionality.
 * Does NOT handle mesh data - use MeshComponent for that.
 *
 * @tparam TMaterial The concrete material type (e.g., LitMaterial)
 */
template <typename MaterialType>
struct MaterialComponent : public IComponent
{
    static_assert(std::is_base_of_v<Material, MaterialType>, "TMaterial must derive from Material");

    using material_t = MaterialType;

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    MaterialType *_material = nullptr; ///< Pointer to material instance (managed by MaterialFactory)

    std::string _materialPath;

  public:

    virtual ~MaterialComponent() = default;

    /**
     * @brief Resolve all resources (textures, etc.)
     * Called by ResourceResolveSystem
     * Derived classes should override this
     */
    virtual bool resolve() { return true; }

    /**
     * @brief Check if resources have been resolved
     */
    // bool isResolved() const {return _material && _material}

    /**
     * @brief Force re-resolve (invalidate cache)
     */
    void invalidate()
    {
        _material = nullptr;
    }


    // ========================================
    // Material Access
    // ========================================


    MaterialType *getRuntimeMaterial() const { return _material; }


    void setRuntimeMaterial(MaterialType *material)
    {
        _material = material;
    }
};


} // namespace ya