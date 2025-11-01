

/**
Lighting
*/
#pragma once


#include "MaterialComponent.h"
#include "Render/Core/Material.h"
namespace ya
{



struct LitMaterial : public Material
{

    struct MaterialUBO
    {
        alignas(16) glm::vec3 baseColor0 = glm::vec3(1.0f);
        alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
        alignas(4) float mixValue        = 0.5f;
        alignas(16) material::TextureParam textureParam0;
        alignas(16) material::TextureParam textureParam1;
    } uMaterial;

    enum
    {
        BaseColor0 = 0,
        BaseColor1 = 1,
    };


    glm::vec3 getBaseColor0() const { return uMaterial.baseColor0; }
    void      setBaseColor0(const glm::vec3 &baseColor0_)
    {
        uMaterial.baseColor0 = baseColor0_;
        setParamDirty();
    }

    glm::vec3 getBaseColor1() const { return uMaterial.baseColor1; }
    void      setBaseColor1(const glm::vec3 &baseColor1_)
    {
        uMaterial.baseColor1 = baseColor1_;
        setParamDirty();
    }

    float getMixValue() const { return uMaterial.mixValue; }
    void  setMixValue(float mixValue_)
    {
        uMaterial.mixValue = mixValue_;
        setParamDirty();
    }

  public:

    [[nodiscard]] MaterialUBO &getParams() { return uMaterial; }
};


struct LitMaterialComponent : public MaterialComponent<LitMaterial>
{
};

} // namespace ya