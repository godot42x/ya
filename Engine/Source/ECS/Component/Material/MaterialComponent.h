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
 * @tparam TMaterial The concrete material type (e.g., PhongMaterial)
 */
template <typename MaterialType>
struct MaterialComponent : public IComponent
{
    static_assert(std::is_base_of_v<Material, MaterialType>, "TMaterial must derive from Material");

    using material_t = MaterialType;
    YA_REFLECT_BEGIN(MaterialComponent<MaterialType>, IComponent)
    YA_REFLECT_FIELD(_materialPath)
    YA_REFLECT_FIELD(_material)
    YA_REFLECT_END()

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    MaterialType *_material        = nullptr; ///< Pointer to material instance (managed by MaterialFactory)
    bool          _bSharedMaterial = false;   ///< If true, material is shared and should not be destroyed by this component

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

    MaterialType *createDefaultMaterial();


    /**
     * @brief Set a shared material (will not be destroyed by this component)
     */
    void setSharedMaterial(MaterialType *material)
    {
        setMaterial(material);
        _bSharedMaterial = true;
    }

    MaterialType *getMaterial() const { return _material; }
    MaterialType *getOrCreateMaterial() const
    {
        if (!_material)
        {
            createDefaultMaterial();
        }
        return _material;
    }


    void setMaterial(MaterialType *material)
    {
        _material = material;
    }
};



} // namespace ya