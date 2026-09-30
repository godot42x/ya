/**
 * @brief PBR Material Component - Serializable PBR material data
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System / MaterialFactory
 * - Mesh data is handled separately by StaticMeshComponent/SkinnedMeshComponent
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

#include "ECS/Component/Material/MaterialComponent.h"
#include "Core/Common/TextureSlot.h"

#include <array>
#include <vector>

namespace ya
{

struct PBRMaterial;
struct MaterialData;

/**
 * @brief Component-local texture slot ids (renderer-independent).
 *
 * Mirrors the slot order of Render3D's PBRMaterial::EResource so the adapter
 * can map 1:1; the component header never names the Render3D enum.
 */
enum class EPBRMaterialTextureSlot : uint8_t
{
    Albedo = 0,
    Normal,
    Metallic,
    Roughness,
    AO,
    Count,
};

/**
 * @brief PBRMaterialComponent - Serializable metallic-roughness PBR component
 *
 * Holds PBR parameters and texture slots for serialization.
 * Runtime material instance is managed separately via MaterialFactory.
 */
struct YA_RENDER_3D_API PBRMaterialComponent : public MaterialComponent<PBRMaterial>
{
    using slot_enum_t = EPBRMaterialTextureSlot;

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
    // Discovery is the scene edit funnel plus slot observers held by the
    // processor; the component subscribes to nothing (ECS storage moves it).
    PBRMaterialComponent() = default;

  private:
    TextureSlot*       getTextureSlotInternal(EPBRMaterialTextureSlot resourceEnum);
    const TextureSlot* getTextureSlotInternal(EPBRMaterialTextureSlot resourceEnum) const;
    void               syncParamsToMaterial();
    void               syncTextureSlot(EPBRMaterialTextureSlot resourceEnum);
    void               importParamsFromDescriptor(const MaterialData& matData);

  public:
    /// Full re-sync of params and texture slots into the runtime material;
    /// idempotent. The processor pump calls it for every queued edit and
    /// every observed slot update.
    EMaterialResolveResult resolve() override;

    void invalidate()
    {
        _resolveState = EMaterialResolveState::Dirty;
    }
    bool                  isResolved() const { return _resolveState == EMaterialResolveState::Ready; }
    EMaterialResolveState getResolveState() const { return _resolveState; }

    TextureSlot*       getTextureSlot(EPBRMaterialTextureSlot r) { return getTextureSlotInternal(r); }
    const TextureSlot* getTextureSlot(EPBRMaterialTextureSlot r) const { return getTextureSlotInternal(r); }

    AuthoringParams&       getParamsMut() { return _params; }
    const AuthoringParams& getParams() const { return _params; }

    TextureSlot* setTextureSlot(EPBRMaterialTextureSlot resourceEnum, const std::string& path)
    {
        switch (resourceEnum) {
        case EPBRMaterialTextureSlot::Albedo:    _albedoSlot.fromPath(path);    break;
        case EPBRMaterialTextureSlot::Normal:    _normalSlot.fromPath(path);    break;
        case EPBRMaterialTextureSlot::Metallic:  _metallicSlot.fromPath(path);  break;
        case EPBRMaterialTextureSlot::Roughness: _roughnessSlot.fromPath(path); break;
        case EPBRMaterialTextureSlot::AO:        _aoSlot.fromPath(path);        break;
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
