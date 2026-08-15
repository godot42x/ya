/**
 * @brief Lit Material Component - Serializable material data
 *
 * Design:
 * - Component holds serializable material data (params + texture slots)
 * - Runtime material instance is created by System
 * - Mesh data is handled separately by StaticMeshComponent/SkinnedMeshComponent
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
#include "Core/Common/TextureSlot.h"

#include <array>
#include <vector>

namespace ya
{

struct PhongMaterial;
struct MaterialData;

/**
 * @brief Component-local texture slot ids (renderer-independent).
 *
 * Mirrors the slot order of Render3D's PhongMaterial::EResource so the adapter
 * can map 1:1; the component header never names the Render3D enum.
 */
enum class EPhongMaterialTextureSlot : uint8_t
{
    Diffuse = 0,
    Specular,
    Reflection,
    Normal,
    Count,
};

/**
 * @brief PhongMaterialComponent - Serializable lit material component
 *
 * Holds material parameters and texture slots for serialization.
 * Runtime material instance is managed separately.
 */
struct YA_RENDER_3D_API PhongMaterialComponent : public MaterialComponent<PhongMaterial>
{
    using slot_enum_t = EPhongMaterialTextureSlot;

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
    struct PropertyChangeSummary
    {
        std::array<bool, static_cast<size_t>(EPhongMaterialTextureSlot::Count)> touchedSlots{};
        bool hasTextureSlotChange     = false;
        bool hasTextureResourceChange = false;
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

    TextureSlot* getTextureSlotInternal(EPhongMaterialTextureSlot resourceEnum);
    const TextureSlot* getTextureSlotInternal(EPhongMaterialTextureSlot resourceEnum) const;
    void syncParamsToMaterial();
    void syncTextureSlot(EPhongMaterialTextureSlot resourceEnum);
    void importParamsFromDescriptor(const MaterialData& matData);
    static PropertyChangeSummary summarizePropertyChanges(const std::vector<std::string>& propPaths);

  public:

    EMaterialResolveResult resolve() override;
    void onPropertyChanged(const std::string& propPath);
    void onPropertiesChanged(const std::vector<std::string>& propPaths);

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
    bool needsResolve() const
    {
        return _resolveState == EMaterialResolveState::Dirty ||
               _resolveState == EMaterialResolveState::Resolving;
    }
    EMaterialResolveState getResolveState() const { return _resolveState; }
    void markResolvedReady() { _resolveState = EMaterialResolveState::Ready; }

    bool checkTexturesStaleness()
    {
        bool stale = false;
        if (_diffuseSlot.textureRef.isStale())    { _diffuseSlot.textureRef.invalidate();    stale = true; }
        if (_specularSlot.textureRef.isStale())    { _specularSlot.textureRef.invalidate();    stale = true; }
        if (_reflectionSlot.textureRef.isStale())  { _reflectionSlot.textureRef.invalidate();  stale = true; }
        if (_normalSlot.textureRef.isStale())      { _normalSlot.textureRef.invalidate();      stale = true; }
        if (stale) {
            invalidate();
        }
        return stale;
    }

    TextureSlot* getTextureSlot(EPhongMaterialTextureSlot resourceEnum)
    {
        return getTextureSlotInternal(resourceEnum);
    }

    const TextureSlot* getTextureSlot(EPhongMaterialTextureSlot resourceEnum) const
    {
        return getTextureSlotInternal(resourceEnum);
    }

    AuthoringParams& getParamsMut() { return _params; }
    const AuthoringParams& getParams() const { return _params; }

    // TextureSlot* getTextureSlot(MatTexture::T type)
    // {
    //     if(type == MatTexture::Diffuse){
    //         return &_diffuseSlot;
    //     }
    // }

    TextureSlot* setTextureSlot(EPhongMaterialTextureSlot resourceEnum, const std::string& path)
    {
        // _textureSlots[resourceEnum].textureRef = TextureRef(path);
        // invalidate();
        // return _textureSlots[resourceEnum];
        switch (resourceEnum) {
        case EPhongMaterialTextureSlot::Diffuse:
            _diffuseSlot.fromPath(path);
            break;
        case EPhongMaterialTextureSlot::Specular:
            _specularSlot.fromPath(path);
            break;
        case EPhongMaterialTextureSlot::Reflection:
            _reflectionSlot.fromPath(path);
            break;
        case EPhongMaterialTextureSlot::Normal:
            _normalSlot.fromPath(path);
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
     * Maps descriptor params to component authoring properties
     *
     * @param matData The material descriptor from model import
     */
    void importFromDescriptor(const MaterialData& matData);

    /**
     * @brief Import material data and bind an existing shared runtime material
     *
     * The component remains the single source of truth for authoring params
     * and texture slots. The shared material is only used as the runtime
     * material instance that resolve() syncs into. The component will NOT own
     * or destroy this material.
     *
     * @param matData The material descriptor from model import
     * @param sharedMaterial Pre-created shared material instance
     */
    void importFromDescriptorWithSharedMaterial(const MaterialData& matData, PhongMaterial* sharedMaterial);
};



} // namespace ya
