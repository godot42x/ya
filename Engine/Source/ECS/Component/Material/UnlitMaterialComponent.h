/**
 * @brief Unlit Material Component - Serializable material data (no lighting)
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System
 * - Mesh data is handled separately by MeshComponent
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
#include "MaterialComponent.h"
#include "Render/Material/UnlitMaterial.h"

#include <array>
#include <vector>

namespace ya
{

// Forward: EMaterialResolveState is defined in PhongMaterialComponent.h
// We reuse it here for consistency.
enum class EMaterialResolveState : uint8_t;


/**
 * @brief UnlitMaterialComponent - Serializable unlit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct UnlitMaterialComponent : public MaterialComponent<UnlitMaterial>
{
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
    UnlitMaterialComponent()
    {
        setupCallbacks();
    }

  private:
    struct EditorChangeSummary
    {
        std::array<bool, UnlitMaterial::EResource::Count> touchedSlots{};
        bool                                              hasTextureSlotChange     = false;
        bool                                              hasTextureResourceChange = false;
    };

    void setupCallbacks()
    {
        auto f = [this]() {
            invalidate();
        };

        _baseColor0Slot.textureRef.onModified.addLambda(this, f);
        _baseColor1Slot.textureRef.onModified.addLambda(this, f);
    }

    TextureSlot*       getTextureSlotInternal(UnlitMaterial::EResource resourceEnum);
    const TextureSlot* getTextureSlotInternal(UnlitMaterial::EResource resourceEnum) const;
    void               syncParamsToMaterial();
    void               syncTextureSlot(UnlitMaterial::EResource resourceEnum);
    static EditorChangeSummary summarizeEditorChanges(const std::vector<std::string>& propPaths);

  public:
    bool resolve() override;
    void onEditorPropertyChanged(const std::string& propPath);
    void onEditorPropertiesChanged(const std::vector<std::string>& propPaths);

    void invalidate()
    {
        _resolveState = EMaterialResolveState::Dirty;
    }
    bool isResolved() const { return _resolveState == EMaterialResolveState::Ready; }
    bool needsResolve() const { return _resolveState == EMaterialResolveState::Dirty || _resolveState == EMaterialResolveState::Failed; }
    EMaterialResolveState getResolveState() const { return _resolveState; }
    void markResolvedReady() { _resolveState = EMaterialResolveState::Ready; }

    TextureSlot* getTextureSlot(UnlitMaterial::EResource resourceEnum)
    {
        return getTextureSlotInternal(resourceEnum);
    }

    const TextureSlot* getTextureSlot(UnlitMaterial::EResource resourceEnum) const
    {
        return getTextureSlotInternal(resourceEnum);
    }

    AuthoringParams& getParamsMut() { return _params; }
    const AuthoringParams& getParams() const { return _params; }

    TextureSlot* setTextureSlot(UnlitMaterial::EResource resourceEnum, const std::string& path)
    {
        switch (resourceEnum) {
        case UnlitMaterial::BaseColor0:
            _baseColor0Slot.fromPath(path);
            break;
        case UnlitMaterial::BaseColor1:
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