#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Material.h"


namespace ya
{

struct PhongMaterial : public Material
{
    struct ParamUBO
    {
        YA_REFLECT_BEGIN(ParamUBO)
        YA_REFLECT_FIELD(ambient)
        YA_REFLECT_FIELD(diffuse)
        YA_REFLECT_FIELD(specular)
        YA_REFLECT_FIELD(shininess, .manipulate(1.0f, 256.0f))
        YA_REFLECT_END()

        alignas(16) glm::vec3 ambient  = glm::vec3(0.1f);
        alignas(16) glm::vec3 diffuse  = glm::vec3(1.0f);
        alignas(16) glm::vec3 specular = glm::vec3(1.0f);
        alignas(4) float shininess     = 32.0f;

        ParamUBO normalize()
        {
            return ParamUBO{
                .ambient   = glm::normalize(ambient),
                .diffuse   = glm::normalize(diffuse),
                .specular  = glm::normalize(specular),
                .shininess = shininess,
            };
        }
    } uParams;


    enum EResource
    {
        DiffuseTexture  = 0,
        SpecularTexture = 1,
    };

    std::unordered_map<PhongMaterial::EResource, TextureView> _textureViews;

    bool bParamDirty    = true;
    bool bResourceDirty = true;


  public:

    [[nodiscard]] const ParamUBO &getParams() { return uParams; }
    ParamUBO                     &getParamsMut() { return uParams; }

    void               setParamDirty(bool bInDirty = true) { bParamDirty = bInDirty; }
    [[nodiscard]] bool isParamDirty() const { return bParamDirty; }

    void               setResourceDirty(bool bInDirty = true) { bResourceDirty = bInDirty; }
    [[nodiscard]] bool isResourceDirty() const { return bResourceDirty; }

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

    void setPhongParam(glm::vec3 ambient, glm::vec3 diffuse, glm::vec3 specular, float shininess)
    {
        uParams.ambient   = ambient;
        uParams.diffuse   = diffuse;
        uParams.specular  = specular;
        uParams.shininess = shininess;
        setParamDirty();
    }
    void setDiffuseParam(const glm::vec3 &diffuse)
    {
        uParams.diffuse = diffuse;
        setParamDirty();
    }

    void setSpecularParam(const glm::vec3 &specular)
    {
        uParams.specular = specular;
        setParamDirty();
    }
    void setShininess(float shininess)
    {
        uParams.shininess = shininess;
        setParamDirty();
    }
};

} // namespace ya