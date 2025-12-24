#pragma once

#include "Material.h"

namespace ya
{

struct LitMaterial : public Material
{
    struct ParamUBO
    {
        alignas(16) glm::vec3 objectColor = glm::vec3(1.0f);
        // alignas(16) glm::vec3 ambient     = glm::vec3(0.1f);
        alignas(16) glm::vec3 diffuse  = glm::vec3(1.0f);
        alignas(16) glm::vec3 specular = glm::vec3(1.0f);
        alignas(4) float shininess     = 32.0f;
        // alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
        // alignas(4) float mixValue        = 0.5f;
        // alignas(16) TextureParam textureParam0;
        // alignas(16) TextureParam textureParam1;
    } uParams;


    enum EResource
    {
        DiffuseTexture = 0,
    };

    std::unordered_map<LitMaterial::EResource, TextureView> _textureViews;

    bool bParamDirty    = true;
    bool bResourceDirty = true;


  public:

    [[nodiscard]] const ParamUBO &getParams() { return uParams; }
    ParamUBO                     &getParamsMut() { return uParams; }

    void               setParamDirty(bool bInDirty = true) { bParamDirty = bInDirty; }
    [[nodiscard]] bool isParamDirty() const { return bParamDirty; }

    void               setResourceDirty(bool bInDirty = true) { bResourceDirty = bInDirty; }
    [[nodiscard]] bool isResourceDirty() const { return bResourceDirty; }


    void setObjectColor(const glm::vec3 &color)
    {
        uParams.objectColor = color;
        setParamDirty();
    }


    // resource
    [[nodiscard]] TextureView *getTextureView(EResource type)
    {
        auto it = _textureViews.find(type);
        if (it != _textureViews.end())
        {
            return &it->second;
        }
        return nullptr;
    }
    TextureView *setTextureView(EResource type, const TextureView &tv)
    {
        _textureViews[type] = tv;
        setResourceDirty();
        return &_textureViews[type];
    }
};

} // namespace ya