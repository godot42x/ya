/**
 * @brief PBR Material Component - Serializable PBR material data
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System / MaterialFactory
 * - Mesh data is handled separately by MeshComponent
 *
 * Serialization format:
 * @code
 * {
 *   "PBRMaterialComponent": {
 *     "_params": { "albedo": [...], "metallic": 0.0, "roughness": 0.5, "ao": 1.0 },
 *     "_albedoSlot": { "textureRef": { "_path": "albedo.png" }, ... },
 *     "_normalSlot": { ... },
 *     "_metallicSlot": { ... },
 *     "_roughnessSlot": { ... },
 *     "_aoSlot": { ... }
 *   }
 * }
 * @endcode
 */
#pragma once

#include "MaterialComponent.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/Model.h" // For MaterialData

#include <array>
#include <vector>

namespace ya
{

/**
 * @brief PBRMaterialComponent - Serializable metallic-roughness PBR component
 *
 * Holds PBR parameters and texture slots for serialization.
 * Runtime material instance is managed separately via MaterialFactory.
 */
struct PBRMaterialComponent : public MaterialComponent<PBRMaterial>
{
    struct AuthoringParams
    {
        YA_REFLECT_BEGIN(AuthoringParams)
        YA_REFLECT_FIELD(albedo, .color())
        YA_REFLECT_FIELD(metallic, .manipulate(0.0f, 1.0f))
        YA_REFLECT_FIELD(roughness, .manipulate(0.0f, 1.0f))
        YA_REFLECT_FIELD(ao, .manipulate(0.0f, 1.0f))
        YA_REFLECT_END()

        glm::vec3 albedo{1.0f};
        float     metallic{0.0f};
        float     roughness{0.5f};
        float     ao{1.0f};
    };

    YA_REFLECT_BEGIN(PBRMaterialComponent, MaterialComponent<PBRMaterial>)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_FIELD(_albedoSlot)
    YA_REFLECT_FIELD(_normalSlot)
    YA_REFLECT_FIELD(_metallicSlot)
    YA_REFLECT_FIELD(_roughnessSlot)
    YA_REFLECT_FIELD(_aoSlot)
    YA_REFLECT_END()

    EMaterialResolveState _resolveState = EMaterialResolveState::Dirty;
    AuthoringParams       _params;

    TextureSlot _albedoSlot;
    TextureSlot _normalSlot;
    TextureSlot _metallicSlot;
    TextureSlot _roughnessSlot;
    TextureSlot _aoSlot;

  public:
    PBRMaterialComponent()
    {
        setupCallbacks();
    }

  private:
    struct EditorChangeSummary
    {
        std::array<bool, PBRMaterial::EResource::Count> touchedSlots{};
        bool                                            hasTextureSlotChange     = false;
        bool                                            hasTextureResourceChange = false;
    };

    void setupCallbacks()
    {
        auto f = [this]() { invalidate(); };
        _albedoSlot.textureRef.onModified.addLambda(this, f);
        _normalSlot.textureRef.onModified.addLambda(this, f);
        _metallicSlot.textureRef.onModified.addLambda(this, f);
        _roughnessSlot.textureRef.onModified.addLambda(this, f);
        _aoSlot.textureRef.onModified.addLambda(this, f);
    }

    TextureSlot*       getTextureSlotInternal(PBRMaterial::EResource resourceEnum);
    const TextureSlot* getTextureSlotInternal(PBRMaterial::EResource resourceEnum) const;
    void               syncParamsToMaterial();
    void               syncTextureSlot(PBRMaterial::EResource resourceEnum);
    void               importParamsFromDescriptor(const MaterialData& matData);
    static EditorChangeSummary summarizeEditorChanges(const std::vector<std::string>& propPaths);

  public:
    bool resolve() override;
    void onEditorPropertyChanged(const std::string& propPath);
    void onEditorPropertiesChanged(const std::vector<std::string>& propPaths);

    void invalidate()
    {
        _resolveState = EMaterialResolveState::Dirty;
    }
    bool                  isResolved() const { return _resolveState == EMaterialResolveState::Ready; }
    bool                  needsResolve() const { return _resolveState == EMaterialResolveState::Dirty || _resolveState == EMaterialResolveState::Failed; }
    EMaterialResolveState getResolveState() const { return _resolveState; }
    void                  markResolvedReady() { _resolveState = EMaterialResolveState::Ready; }

    TextureSlot*       getTextureSlot(PBRMaterial::EResource r) { return getTextureSlotInternal(r); }
    const TextureSlot* getTextureSlot(PBRMaterial::EResource r) const { return getTextureSlotInternal(r); }

    AuthoringParams&       getParamsMut() { return _params; }
    const AuthoringParams& getParams() const { return _params; }

    TextureSlot* setTextureSlot(PBRMaterial::EResource resourceEnum, const std::string& path)
    {
        switch (resourceEnum) {
        case PBRMaterial::AlbedoTexture:    _albedoSlot.fromPath(path);    break;
        case PBRMaterial::NormalTexture:    _normalSlot.fromPath(path);    break;
        case PBRMaterial::MetallicTexture:  _metallicSlot.fromPath(path);  break;
        case PBRMaterial::RoughnessTexture: _roughnessSlot.fromPath(path); break;
        case PBRMaterial::AOTexture:        _aoSlot.fromPath(path);        break;
        default: break;
        }
        invalidate();
        return getTextureSlot(resourceEnum);
    }

    void syncTextureSlots();

    void importFromDescriptor(const MaterialData& matData);
    void importFromDescriptorWithSharedMaterial(const MaterialData& matData, PBRMaterial* sharedMaterial);
};

} // namespace ya
