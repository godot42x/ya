/**
 * @brief Unlit Material Component - Serializable material data (no lighting)
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System
 * - Mesh data is handled separately by StaticMeshComponent/SkinnedMeshComponent
 *
 * Serialization format:
 * @code
 * {
 *   "UnlitMaterialComponent": {
 *     "_params": { "baseColor0": [...], "baseColor1": [...], "mixValue": 0.5 },
 *     "_baseColor0Slot": { "textureRef": { "_path": "tex0.png" }, ... },
 *     "_baseColor1Slot": { "textureRef": { "_path": "tex1.png" }, ... }
 *   }
 * }
 * @endcode
 */
#pragma once

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/MaterialComponent.h"
#include "Core/Common/TextureSlot.h"

#include <array>
#include <vector>

namespace ya
{

struct UnlitMaterial;

/**
 * @brief Component-local texture slot ids (renderer-independent).
 *
 * Mirrors the slot order of Render3D's UnlitMaterial::EResource so the adapter
 * can map 1:1; the component header never names the Render3D enum.
 */
enum class EUnlitMaterialTextureSlot : uint8_t
{
    BaseColor0 = 0,
    BaseColor1,
    Count,
};

// Forward: EMaterialResolveState is defined in PhongMaterialComponent.h
// We reuse it here for consistency.
enum class EMaterialResolveState : uint8_t;


/**
 * @brief UnlitMaterialComponent - Serializable unlit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct YA_RENDER_3D_API UnlitMaterialComponent : public MaterialComponent<UnlitMaterial>
{
    using slot_enum_t = EUnlitMaterialTextureSlot;

    struct AuthoringParams
    {
        YA_REFLECT_BEGIN(AuthoringParams)
        YA_REFLECT_FIELD(baseColor0, .color())
        YA_REFLECT_FIELD(baseColor1, .color())
        YA_REFLECT_FIELD(mixValue, .manipulate(0.0f, 1.0f))
        YA_REFLECT_END()

        glm::vec3 baseColor0{1.0f};
        glm::vec3 baseColor1{1.0f};
        float     mixValue{0.5f};
    };

    YA_REFLECT_BEGIN(UnlitMaterialComponent, MaterialComponent<UnlitMaterial>)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_FIELD(_baseColor0Slot)
    YA_REFLECT_FIELD(_baseColor1Slot)
    YA_REFLECT_END()

    EMaterialResolveState _resolveState = EMaterialResolveState::Dirty;
    AuthoringParams       _params;

    TextureSlot _baseColor0Slot;
    TextureSlot _baseColor1Slot;

  public:
    // Discovery is the scene edit funnel plus slot observers held by the
    // processor; the component subscribes to nothing (ECS storage moves it).
    UnlitMaterialComponent() = default;

  private:
    TextureSlot*       getTextureSlotInternal(EUnlitMaterialTextureSlot resourceEnum);
    const TextureSlot* getTextureSlotInternal(EUnlitMaterialTextureSlot resourceEnum) const;
    void               syncParamsToMaterial();
    void               syncTextureSlot(EUnlitMaterialTextureSlot resourceEnum);

  public:
    /// Full re-sync of params and texture slots into the runtime material;
    /// idempotent. The processor pump calls it for every queued edit and
    /// every observed slot update.
    EMaterialResolveResult resolve() override;

    void invalidate()
    {
        _resolveState = EMaterialResolveState::Dirty;
    }
    bool isResolved() const { return _resolveState == EMaterialResolveState::Ready; }
    EMaterialResolveState getResolveState() const { return _resolveState; }

    TextureSlot* getTextureSlot(EUnlitMaterialTextureSlot resourceEnum)
    {
        return getTextureSlotInternal(resourceEnum);
    }

    const TextureSlot* getTextureSlot(EUnlitMaterialTextureSlot resourceEnum) const
    {
        return getTextureSlotInternal(resourceEnum);
    }

    AuthoringParams& getParamsMut() { return _params; }
    const AuthoringParams& getParams() const { return _params; }

    TextureSlot* setTextureSlot(EUnlitMaterialTextureSlot resourceEnum, const std::string& path)
    {
        switch (resourceEnum) {
        case EUnlitMaterialTextureSlot::BaseColor0:
            _baseColor0Slot.fromPath(path);
            break;
        case EUnlitMaterialTextureSlot::BaseColor1:
            _baseColor1Slot.fromPath(path);
            break;
        default:
            break;
        }
        invalidate();
        return getTextureSlot(resourceEnum);
    }

    void syncTextureSlots();
};

} // namespace ya
