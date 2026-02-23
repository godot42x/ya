#pragma once
#include "Render/Material/Material.h"

namespace ya
{



struct UnlitMaterial : public Material
{

  public:
    using Self = UnlitMaterial;

    DISABLE_PADDED_STRUCT_WARNING_BEGIN()
    struct TextureParam
    {
        bool enable{false};
        alignas(4) float uvRotation{0.0f};
        // x,y scale, z,w translate
        alignas(16) glm::vec4 uvTransform{1.0f, 1.0f, 0.0f, 0.0f};

        void setUVParams(glm::vec2 scale, glm::vec2 offset, float rotation)
        {
            uvRotation  = rotation;
            uvTransform = {scale.x, scale.y, offset.x, offset.y};
        }

        void updateByTextureView(const TextureView* tv)
        {
            enable = tv->bEnable && tv->isValid();
        }
    };

    struct MaterialUBO
    {
        alignas(16) glm::vec3 baseColor0 = glm::vec3(1.0f);
        alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
        alignas(4) float mixValue        = 0.5f;
        alignas(16) TextureParam textureParam0;
        alignas(16) TextureParam textureParam1;
    };
    DISABLE_PADDED_STRUCT_WARNING_END()

    // indicate the color in MaterialUBO param
    enum
    {
        BaseColor0 = 0,
        BaseColor1 = 1,
    };

  public:


    std::unordered_map<uint32_t, TextureView> _textureViews;


    MaterialUBO uMaterial;

    int  _dirtyMask     = 0;
    bool bParamDirty    = false;
    bool bResourceDirty = false; //  indicate that the resource of current material (texture etc.) need to be updated in GPU side

  public:


    void               setParamDirty(bool bDirty = true) { bParamDirty = bDirty; }
    [[nodiscard]] bool isParamDirty() const { return bParamDirty; }

    [[nodiscard]] bool isResourceDirty() const { return bResourceDirty; }
    void               setResourceDirty(bool bDirty = true) { bResourceDirty = bDirty; }


    // MARK: resources api
    TextureView*                     setTextureView(uint32_t type, const TextureView& tv);
    [[nodiscard]] bool               hasTextureView(uint32_t type) const;
    [[nodiscard]] const TextureView* getTextureView(uint32_t type) const;
    TextureView*                     getTextureViewMut(uint32_t type);

    void setTextureViewTexture(uint32_t type, stdptr<Texture> texture);
    void setTextureViewSampler(uint32_t type, stdptr<Sampler> sampler);
    void setTextureViewEnable(uint32_t type, bool benable);
    void setTextureViewUVTranslation(uint32_t type, const glm::vec2& uvTranslation);
    void setTextureViewUVScale(uint32_t type, const glm::vec2& uvScale);
    void setTextureViewUVRotation(uint32_t type, float uvRotation);

    // Get mutable texture param for UV editing
    TextureParam* getTextureParamMut(uint32_t type)
    {
        if (type == BaseColor0) return &uMaterial.textureParam0;
        if (type == BaseColor1) return &uMaterial.textureParam1;
        return nullptr;
    }

    // MARK: params api
    glm::vec3           getBaseColor0() const { return uMaterial.baseColor0; }
    void                setBaseColor0(const glm::vec3& baseColor0_);
    glm::vec3           getBaseColor1() const { return uMaterial.baseColor1; }
    void                setBaseColor1(const glm::vec3& baseColor1_);
    [[nodiscard]] float getMixValue() const { return uMaterial.mixValue; }
    void                setMixValue(float mixValue_);

  public:

    [[nodiscard]] MaterialUBO& getParams() { return uMaterial; }
};
} // namespace ya