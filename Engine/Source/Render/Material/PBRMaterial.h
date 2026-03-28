#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Material.h"
#include "Render/Core/Std140Types.h"

#include "DeferredRender.PBR_GBufferPass.slang.h"

namespace ya
{

/**
 * @brief PBRMaterial - Metallic-Roughness PBR material for rendering
 *
 * Design:
 * - Component (PBRMaterialComponent) holds serializable data (params + texture slots)
 * - Material (PBRMaterial) manages runtime rendering state (texture bindings)
 * - Data sync happens automatically during resolve()
 *
 * ParamUBO uses the slang-generated type as single source of truth.
 */
struct PBRMaterial : public Material
{
    // ========================================
    // Nested Types
    // ========================================

    /// Texture resource enum — standard metallic-roughness workflow
    enum EResource : int
    {
        AlbedoTexture    = 0,
        NormalTexture    = 1,
        MetallicTexture  = 2,
        RoughnessTexture = 3,
        AOTexture        = 4,
        Count,
    };

    /// GPU UBO struct — sourced from shader, do NOT redefine in C++.
    using ParamUBO = slang_types::DeferredRender::PBR_GBufferPass::PBRParamsData;

    // ========================================
    // Reflection Registration
    // ========================================
    YA_REFLECT_BEGIN(PBRMaterial, Material)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_END()

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    ParamUBO                                     _params{};
    std::array<TextureBinding, EResource::Count> _textureBindings;

  public:
    // ========================================
    // Parameter Accessors
    // ========================================
    [[nodiscard]] const ParamUBO& getParams() const { return _params; }
    ParamUBO&                     getParamsMut() { return _params; }

    void setPBRParams(const glm::vec3& albedo, float metallic, float roughness, float ao)
    {
        _params.albedo    = albedo;
        _params.metallic  = metallic;
        _params.roughness = roughness;
        _params.ao        = ao;
        setParamDirty();
    }

    void setAlbedo(const glm::vec3& albedo)
    {
        _params.albedo = albedo;
        setParamDirty();
    }

    void setMetallic(float metallic)
    {
        _params.metallic = metallic;
        setParamDirty();
    }

    void setRoughness(float roughness)
    {
        _params.roughness = roughness;
        setParamDirty();
    }

    void setAO(float ao)
    {
        _params.ao = ao;
        setParamDirty();
    }

    // ========================================
    // Runtime TextureBinding
    // ========================================

    [[nodiscard]] const TextureBinding& getTextureBinding(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)];
    }

    [[nodiscard]] TextureBinding& getTextureBindingMut(EResource type)
    {
        return _textureBindings[static_cast<size_t>(type)];
    }

    void setTextureBinding(EResource type, const TextureBinding& tb)
    {
        _textureBindings[static_cast<size_t>(type)] = tb;
        setResourceDirty();
    }

    void clearTextureBinding(EResource type)
    {
        _textureBindings[static_cast<size_t>(type)].clear();
        setResourceDirty();
    }

    void clearTextureBindings()
    {
        for (auto& tb : _textureBindings) {
            tb.clear();
        }
        setResourceDirty();
    }

    [[nodiscard]] ImageViewHandle getImageViewHandle(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)].getImageViewHandle();
    }

    [[nodiscard]] SamplerHandle getSamplerHandle(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)].getSamplerHandle();
    }

    // ========================================
    // Virtual Interface Implementation
    // ========================================

    int getTextureSlotEnum(const std::string& name) const override
    {
        if (name == "albedo" || name == "diffuse" || name == "baseColor") return AlbedoTexture;
        if (name == "normal") return NormalTexture;
        if (name == "metallic") return MetallicTexture;
        if (name == "roughness") return RoughnessTexture;
        if (name == "ao" || name == "occlusion") return AOTexture;
        return -1;
    }

    bool resolveTextures() override { return true; }
};

} // namespace ya

YA_REFLECT_BEGIN_EXTERNAL(ya::PBRMaterial::ParamUBO)
YA_REFLECT_FIELD(albedo, .color())
YA_REFLECT_FIELD(metallic, .manipulate(0.0f, 1.0f))
YA_REFLECT_FIELD(roughness, .manipulate(0.0f, 1.0f))
YA_REFLECT_FIELD(ao, .manipulate(0.0f, 1.0f))
YA_REFLECT_END_EXTERNAL()
