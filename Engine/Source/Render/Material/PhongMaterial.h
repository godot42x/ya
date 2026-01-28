#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Material.h"


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

    /// GPU-aligned parameter UBO (defined in Component, reused here)
    struct ParamUBO
    {
        alignas(16) glm::vec3 ambient  = glm::vec3(0.1f);
        alignas(16) glm::vec3 diffuse  = glm::vec3(1.0f);
        alignas(16) glm::vec3 specular = glm::vec3(1.0f);
        alignas(4) float shininess     = 32.0f;
        // alignas(8) glm::vec2 uvs

        ParamUBO normalize() const
        {
            return ParamUBO{
                .ambient   = glm::normalize(ambient),
                .diffuse   = glm::normalize(diffuse),
                .specular  = glm::normalize(specular),
                .shininess = shininess,
            };
        }
    };

    /// Texture resource enum
    enum EResource : int
    {
        DiffuseTexture  = 0,
        SpecularTexture = 1,
        // Extend here for normal map, etc.
    };

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
    [[nodiscard]] const ParamUBO &getParams() const { return _params; }
    ParamUBO                     &getParamsMut() { return _params; }

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
    void setDiffuseParam(const glm::vec3 &diffuse)
    {
        _params.diffuse = diffuse;
        setParamDirty();
    }

    /**
     * @brief Set specular parameter (synced from Component)
     */
    void setSpecularParam(const glm::vec3 &specular)
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

    // ========================================
    // Runtime TextureView Access (For Rendering)
    // ========================================

    /**
     * @brief Get resolved texture view for rendering
     */
    [[nodiscard]] TextureView *getTextureView(EResource type)
    {
        auto it = _textureViews.find(static_cast<int>(type));
        return it != _textureViews.end() ? &it->second : nullptr;
    }

    /**
     * @brief Set texture view directly (called by Component/Resolver)
     */
    TextureView *setTextureView(EResource type, const TextureView &tv)
    {
        _textureViews[static_cast<int>(type)] = tv;
        setResourceDirty();
        return &_textureViews[static_cast<int>(type)];
    }

    /**
     * @brief Clear texture views (called on re-resolve)
     */
    void clearTextureViews()
    {
        _textureViews.clear();
        setResourceDirty();
    }

    // ========================================
    // Virtual Interface Implementation
    // ========================================

    const char *getTextureSlotName(int resourceEnum) const override
    {
        switch (static_cast<EResource>(resourceEnum)) {
        case DiffuseTexture:
            return "diffuse";
        case SpecularTexture:
            return "specular";
        default:
            return "unknown";
        }
    }

    int getTextureSlotEnum(const std::string &name) const override
    {
        if (name == "diffuse") return DiffuseTexture;
        if (name == "specular") return SpecularTexture;
        return -1;
    }



    bool resolveTextures() override { return true; /* Needs sampler, use overload */ }
};


} // namespace ya

// Reflection for ParamUBO (cannot be inside class due to memory layout)
YA_REFLECT_BEGIN_EXTERNAL(ya::PhongMaterial::ParamUBO)
YA_REFLECT_FIELD(ambient, .color())
YA_REFLECT_FIELD(diffuse, .color())
YA_REFLECT_FIELD(specular, .color())
YA_REFLECT_FIELD(shininess, .manipulate(1.0f, 256.0f))
YA_REFLECT_END_EXTERNAL()