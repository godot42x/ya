#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Material.h"
#include "Render/Core/Std140Types.h"

#include "Test.Unlit.glsl.h"

namespace ya
{

/**
 * @brief UnlitMaterial - Unlit material for rendering (no lighting)
 *
 * Design:
 * - Component (UnlitMaterialComponent) holds serializable data (params + texture slots)
 * - Material (UnlitMaterial) manages runtime rendering state (texture views)
 * - Data sync happens automatically during resolve()
 *
 * Memory: ~48 bytes (only texture views cache, no duplicate params)
 */
struct UnlitMaterial : public Material
{
    // ========================================
    // Nested Types
    // ========================================

    struct alignas(16) TextureParam
    {
        std140::b32  bEnable{false};
        std140::mat3 uvTransform{1.0f};
    };

    /// Texture resource enum
    enum EResource : int
    {
        BaseColor0 = 0,
        BaseColor1 = 1,
        Count,
    };

YA_DISABLE_PADDED_STRUCT_WARNING_BEGIN()
    /// GPU UBO struct - std140 compatible types, direct upload without pack
    struct ParamUBO
    {
        alignas(16) glm::vec3 baseColor0{1.0f};  // 16 bytes
        alignas(16) glm::vec3 baseColor1{1.0f};  // 16 bytes
        alignas(4) float mixValue{0.5f};          // 4 bytes

        std::array<TextureParam, EResource::Count> textureParams;
    };
    // C++ side uses std140::b32 + std140::mat3 for reflection metadata & type safety.
    // Cross-validate with shader companion to ensure layout consistency.
    static_assert(sizeof(TextureParam) == sizeof(glsl_types::Test::Unlit::TextureParam),
                  "TextureParam size mismatch with shader companion");
    static_assert(sizeof(ParamUBO) == sizeof(glsl_types::Test::Unlit::MaterialUBO),
                  "ParamUBO size mismatch with shader companion (MaterialUBO)");
YA_DISABLE_PADDED_STRUCT_WARNING_END()

    // ========================================
    // Reflection Registration
    // ========================================
    YA_REFLECT_BEGIN(UnlitMaterial, Material)
    YA_REFLECT_FIELD(_params)
    YA_REFLECT_END()

    // ========================================
    // Runtime State (Not Serialized)
    // ========================================
    ParamUBO _params;

  public:
    // ========================================
    // Parameter Accessors
    // ========================================
    [[nodiscard]] const ParamUBO& getParams() const { return _params; }
    ParamUBO&                     getParamsMut() { return _params; }

    void setTextureParam(EResource type, bool bEnable, const glm::mat3& uvTransform)
    {
        auto& param        = _params.textureParams[static_cast<size_t>(type)];
        param.bEnable      = bEnable;
        param.uvTransform  = uvTransform;
        setParamDirty();
    }

    void disableTextureParam(EResource type)
    {
        setTextureParam(type, false, glm::mat3(1.0f));
    }

    // ========================================
    // Runtime TextureBinding (Replaces TextureView)
    // ========================================
    std::array<TextureBinding, EResource::Count> _textureBindings;

    /**
     * @brief Get texture binding for a resource slot
     */
    [[nodiscard]] const TextureBinding& getTextureBinding(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)];
    }

    [[nodiscard]] TextureBinding& getTextureBindingMut(EResource type)
    {
        return _textureBindings[static_cast<size_t>(type)];
    }

    /**
     * @brief Set texture binding (called by Component/Resolver)
     */
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

    /**
     * @brief Clear all texture bindings (called on re-resolve)
     */
    void clearTextureBindings()
    {
        for (auto& tb : _textureBindings) {
            tb.clear();
        }
        setResourceDirty();
    }

    /**
     * @brief Get GPU image-view handle with fallback (white texture)
     */
    [[nodiscard]] ImageViewHandle getImageViewHandle(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)].getImageViewHandle();
    }

    /**
     * @brief Get GPU sampler handle with fallback (default sampler)
     */
    [[nodiscard]] SamplerHandle getSamplerHandle(EResource type) const
    {
        return _textureBindings[static_cast<size_t>(type)].getSamplerHandle();
    }

    // ========================================
    // Virtual Interface Implementation
    // ========================================

    int getTextureSlotEnum(const std::string& name) const override
    {
        if (name == "baseColor0") return BaseColor0;
        if (name == "baseColor1") return BaseColor1;
        return -1;
    }

    bool resolveTextures() override { return true; /* Needs sampler, use overload */ }
};

} // namespace ya

YA_REFLECT_BEGIN_EXTERNAL(ya::UnlitMaterial::TextureParam)
YA_REFLECT_FIELD(uvTransform)
YA_REFLECT_FIELD(bEnable)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::UnlitMaterial::ParamUBO)
YA_REFLECT_FIELD(baseColor0, .color())
YA_REFLECT_FIELD(baseColor1, .color())
YA_REFLECT_FIELD(mixValue, .manipulate(0.0f, 1.0f))
YA_REFLECT_FIELD(textureParams)
YA_REFLECT_END_EXTERNAL()