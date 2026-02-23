/**
 * @brief Lit Material Component - Serializable material data
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System
 * - Mesh data is handled separately by MeshComponent
 *
 * Serialization format:
 * @code
 * {
 *   "PhongMaterialComponent": {
 *     "_params": { "ambient": [...], "diffuse": [...], "specular": [...], "shininess": 32.0 },
 *     "_textureSlots": {
 *       "0": { "textureRef": { "_path": "diffuse.png" }, ... },
 *       "1": { "textureRef": { "_path": "specular.png" }, ... }
 *     }
 *   }
 * }
 * @endcode
 */
#pragma once

#include "MaterialComponent.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Model.h" // For MaterialData

namespace ya
{


/**
 * @brief PhongMaterialComponent - Serializable lit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct PhongMaterialComponent : public MaterialComponent<PhongMaterial>
{
    YA_REFLECT_BEGIN(PhongMaterialComponent, MaterialComponent<PhongMaterial>)
    // YA_REFLECT_FIELD(_textureSlots)
    YA_REFLECT_FIELD(_diffuseSlot)
    YA_REFLECT_FIELD(_specularSlot)
    YA_REFLECT_FIELD(_reflectionSlot)
    YA_REFLECT_END()


    bool _bResolved = false;

    // TextureSlotMap _textureSlots;
    TextureSlot _diffuseSlot;
    TextureSlot _specularSlot;
    TextureSlot _reflectionSlot;

  public:
    PhongMaterialComponent()
    {
        setupCallbacks();
    }


  private:
    void setupCallbacks()
    {
        auto f = [this]() {
            invalidate();
        };

        _diffuseSlot.textureRef.onModified.addLambda(this, f);
        _specularSlot.textureRef.onModified.addLambda(this, f);
        _reflectionSlot.textureRef.onModified.addLambda(this, f);
    }

  public:

    bool resolve() override;

    void invalidate()
    {
        // _material = nullptr;
        // for (auto &[t, slot] : _textureSlots)
        // {
        //     slot.invalidate();
        // }
        _bResolved = false;
    }
    bool isResolved() const { return _bResolved; }

    TextureSlot* getTextureSlot(PhongMaterial::EResource resourceEnum)
    {
        // return _textureSlots[resourceEnum];
        switch (resourceEnum) {
        case PhongMaterial::DiffuseTexture:
            return &_diffuseSlot;
        case PhongMaterial::SpecularTexture:
            return &_specularSlot;
        case PhongMaterial::ReflectionTexture:
            return &_reflectionSlot;
        default:
            return nullptr;
        }
    }
    TextureSlot* setTextureSlot(PhongMaterial::EResource resourceEnum, const std::string& path)
    {
        // _textureSlots[resourceEnum].textureRef = TextureRef(path);
        // invalidate();
        // return _textureSlots[resourceEnum];
        switch (resourceEnum) {
        case PhongMaterial::DiffuseTexture:
            _diffuseSlot.textureRef.setPath(path);
            break;
        case PhongMaterial::SpecularTexture:
            _specularSlot.textureRef.setPath(path);
            break;
        default:
            break;
        }
        invalidate();
        return getTextureSlot(resourceEnum);
    }

    void syncTextureSlots();

    /**
     * @brief Import material data from a generic MaterialData descriptor
     * Maps descriptor params to Phong-specific properties
     *
     * @param matData The material descriptor from model import
     * @param syncParams If true, sync parameters to runtime material (default: true)
     */
    void importFromDescriptor(const MaterialData& matData, bool syncParams = true);

    /**
     * @brief Import material data and use an existing shared material
     *
     * This sets up the component's texture slots from the descriptor,
     * while using an externally provided shared material instance.
     * The component will NOT own or destroy this material.
     *
     * @param matData The material descriptor from model import
     * @param sharedMaterial Pre-created shared material instance
     */
    void importFromDescriptorWithSharedMaterial(const MaterialData& matData, PhongMaterial* sharedMaterial);
};



} // namespace ya