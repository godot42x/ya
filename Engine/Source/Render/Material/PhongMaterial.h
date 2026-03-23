#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Material.h"
#include "Render/Core/Std140Types.h"

#include "PhongLit.Types.glsl.h"

namespace ya
{


/**
 * @brief PhongMaterial - Phong-based lit material for rendering
 *
 * Design:
 * - Component (PhongMaterialComponent) holds serializable data (params + texture slots)
 * - Material (PhongMaterial) manages runtime rendering state (texture views)
 * - Data sync happens automatically during resolve()
 *
 * Memory: ~48 bytes (only texture views cache, no duplicate params)
 */
struct PhongMaterial : public Material
{
    // ========================================
    // Nested Types
    // ========================================

    // struct alignas(16   )
    //     TextureParam
    // {
    //     std140::b32  bEnable{false};
    //     std140::mat3 uvTransform{1.0f};
    // };

    using TextureParam = glsl_types::PhongLit::Types::TextureParam;

    /// Texture resource enum
    enum EResource : int
    {
        DiffuseTexture    = 0,
        SpecularTexture   = 1,
        ReflectionTexture = 2,
        NormalTexture = 3,
        // Extend here for normal map, etc.
        Count,
    };

YA_DISABLE_PADDED_STRUCT_WARNING_BEGIN()
    /// GPU UBO 结构 - 使用 std140 兼容类型，直接上传无需 pack
    struct ParamUBO
    {
        alignas(16) glm::vec3 ambient{1.0f};  // 16 bytes
        alignas(16) glm::vec3 diffuse{1.0f};  // 16 bytes
        alignas(16) glm::vec3 specular{1.0f}; // 16 bytes
        alignas(4) float shininess{32.0f};    // 4 bytes

        std::array<TextureParam, EResource::Count> textureParams;
    };

    // 暂时需要反射查看实际value，不去动头文件生成逻辑(未来反射框架定下来之后可以做拓展也生产代码代码)
    // using ParamData = glsl_types::PhongLit::Types::ParamUBO
YA_DISABLE_PADDED_STRUCT_WARNING_END()


    // ========================================
    // Reflection Registration
    // ========================================
    YA_REFLECT_BEGIN(PhongMaterial, Material)
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

    /**
     * @brief Set all Phong parameters (synced from Component)
     */
    void setPhongParam(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess)
    {
        _params.ambient   = ambient;
        _params.diffuse   = diffuse;
        _params.specular  = specular;
        _params.shininess = shininess;
        setParamDirty();
    }

    /**
     * @brief Set diffuse parameter (synced from Component)
     */
    void setDiffuseParam(const glm::vec3& diffuse)
    {
        _params.diffuse = diffuse;
        setParamDirty();
    }

    /**
     * @brief Set specular parameter (synced from Component)
     */
    void setSpecularParam(const glm::vec3& specular)
    {
        _params.specular = specular;
        setParamDirty();
    }

    /**
     * @brief Set shininess parameter (synced from Component)
     */
    void setShininess(float shininess)
    {
        _params.shininess = shininess;
        setParamDirty();
    }

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
        if (name == "diffuse") return DiffuseTexture;
        if (name == "specular") return SpecularTexture;
        if (name == "reflection") return ReflectionTexture;
        return -1;
    }



    bool resolveTextures() override { return true; /* Needs sampler, use overload */ }
};


} // namespace ya

YA_REFLECT_BEGIN_EXTERNAL(ya::PhongMaterial::TextureParam)
YA_REFLECT_FIELD(uvTransform)
YA_REFLECT_FIELD(bEnable)
YA_REFLECT_END_EXTERNAL()

YA_REFLECT_BEGIN_EXTERNAL(ya::PhongMaterial::ParamUBO)
YA_REFLECT_FIELD(ambient, .color())
YA_REFLECT_FIELD(diffuse, .color())
YA_REFLECT_FIELD(specular, .color())
YA_REFLECT_FIELD(shininess, .manipulate(1.0f, 256.0f))
YA_REFLECT_FIELD(textureParams)
YA_REFLECT_END_EXTERNAL()