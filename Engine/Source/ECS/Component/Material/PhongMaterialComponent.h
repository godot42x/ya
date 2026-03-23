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

#include <array>
#include <vector>

namespace ya
{

enum class EMaterialResolveState : uint8_t
{
    Dirty = 0,
    Resolving,
    Ready,
    Failed,
};


/**
 * @brief PhongMaterialComponent - Serializable lit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct PhongMaterialComponent : public MaterialComponent<PhongMaterial>
{
    struct AuthoringParams
    {
        YA_REFLECT_BEGIN(AuthoringParams)
        YA_REFLECT_FIELD(ambient, .color())
        YA_REFLECT_FIELD(diffuse, .color())
        YA_REFLECT_FIELD(specular, .color())
        YA_REFLECT_FIELD(shininess, .manipulate(1.0f, 256.0f))
        YA_REFLECT_END()

        glm::vec3 ambient{1.0f};
        glm::vec3 diffuse{1.0f};
        glm::vec3 specular{1.0f};
        float     shininess{32.0f};
    };

    YA_REFLECT_BEGIN(PhongMaterialComponent, MaterialComponent<PhongMaterial>)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_FIELD(_diffuseSlot)
    YA_REFLECT_FIELD(_specularSlot)
    YA_REFLECT_FIELD(_reflectionSlot)
    YA_REFLECT_FIELD(_normalSlot)
    YA_REFLECT_END()


    EMaterialResolveState _resolveState = EMaterialResolveState::Dirty;
    AuthoringParams _params;

    // TextureSlotMap _textureSlots;
    TextureSlot _diffuseSlot;
    TextureSlot _specularSlot;
    TextureSlot _reflectionSlot;
    TextureSlot _normalSlot;

  public:
    PhongMaterialComponent()
    {
        setupCallbacks();
    }


  private:
    struct EditorChangeSummary
    {
        std::array<bool, PhongMaterial::EResource::Count> touchedSlots{};
        bool                                              hasTextureSlotChange     = false;
        bool                                              hasTextureResourceChange = false;
    };

    void setupCallbacks()
    {
        auto f = [this]() {
            invalidate();
        };

        _diffuseSlot.textureRef.onModified.addLambda(this, f);
        _specularSlot.textureRef.onModified.addLambda(this, f);
        _reflectionSlot.textureRef.onModified.addLambda(this, f);
        _normalSlot.textureRef.onModified.addLambda(this, f);
    }

    TextureSlot* getTextureSlotInternal(PhongMaterial::EResource resourceEnum);
    const TextureSlot* getTextureSlotInternal(PhongMaterial::EResource resourceEnum) const;
    void syncParamsToMaterial();
    void syncTextureSlot(PhongMaterial::EResource resourceEnum);
    void importParamsFromDescriptor(const MaterialData& matData);
    static EditorChangeSummary summarizeEditorChanges(const std::vector<std::string>& propPaths);

  public:

    bool resolve() override;
    void onEditorPropertyChanged(const std::string& propPath);
    void onEditorPropertiesChanged(const std::vector<std::string>& propPaths);

    void invalidate()
    {
        // _material = nullptr;
        // for (auto &[t, slot] : _textureSlots)
        // {
        //     slot.invalidate();
        // }
        _resolveState = EMaterialResolveState::Dirty;
    }
    bool isResolved() const { return _resolveState == EMaterialResolveState::Ready; }
    bool needsResolve() const { return _resolveState == EMaterialResolveState::Dirty || _resolveState == EMaterialResolveState::Failed; }
    EMaterialResolveState getResolveState() const { return _resolveState; }
    void markResolvedReady() { _resolveState = EMaterialResolveState::Ready; }

    TextureSlot* getTextureSlot(PhongMaterial::EResource resourceEnum)
    {
        return getTextureSlotInternal(resourceEnum);
    }

    // TextureSlot* getTextureSlot(MatTexture::T type)
    // {
    //     if(type == MatTexture::Diffuse){
    //         return &_diffuseSlot;
    //     }
    // }

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
        case PhongMaterial::ReflectionTexture:
            _reflectionSlot.textureRef.setPath(path);
            break;
        case PhongMaterial::NormalTexture:
            _normalSlot.textureRef.setPath(path);
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