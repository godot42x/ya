#pragma once

#include "Core/Base.h"
#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Render/Material/Material.h"
#include "Render/Material/MaterialFactory.h"


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
    YA_REFLECT_END()

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    MaterialType* _material        = nullptr; ///< Pointer to material instance (managed by MaterialFactory)
    bool          _bSharedMaterial = false;   ///< If true, material is shared and should not be destroyed by this component

    std::string _materialPath;

  public:

    virtual ~MaterialComponent()
    {
        releaseMaterial();
    }

    /**
     * @brief Resolve all resources (textures, etc.)
     * Called by ResourceResolveSystem
     * Derived classes should override this
     */
    virtual EMaterialResolveResult resolve() { return EMaterialResolveResult::Ready; }

    /**
     * @brief Check if resources have been resolved
     */
    // bool isResolved() const {return _material && _material}

    /**
     * @brief Force re-resolve (invalidate cache)
     */
    void invalidate()
    {
        releaseMaterial();
    }

    void releaseMaterial()
    {
        if (_material && !_bSharedMaterial) {
            if (auto* factory = MaterialFactory::get()) {
                factory->destroyMaterial(_material);
            }
        }
        _material        = nullptr;
        _bSharedMaterial = false;
    }

    MaterialType* createDefaultMaterial()
    {
        releaseMaterial();
        std::string matLabel = typeid(MaterialType).name() + std::to_string(reinterpret_cast<uintptr_t>(this));
        _material            = MaterialFactory::get()->createMaterial<MaterialType>(matLabel);
        _bSharedMaterial     = false; // Created our own material
        return _material;
    }


    /**
     * @brief Set a shared material (will not be destroyed by this component)
     */
    void setSharedMaterial(MaterialType* material)
    {
        if (_material == material && _bSharedMaterial) {
            return;
        }

        releaseMaterial();
        _material        = material;
        _bSharedMaterial = (_material != nullptr);
    }

    MaterialType* getMaterial() const { return _material; }
    MaterialType* getOrCreateMaterial()
    {
        if (!_material)
        {
            createDefaultMaterial();
        }
        return _material;
    }


    void setMaterial(MaterialType* material)
    {
        setSharedMaterial(material);
    }

    bool isUsingSharedMaterial() const { return _material != nullptr && _bSharedMaterial; }
};



} // namespace ya
