
/**
Unlit(no lighting )
*/
#pragma once


#include "MaterialComponent.h"
#include "Render/Core/Material.h"
namespace ya
{



struct FrameUBO
{
    glm::mat4 projection{1.f};
    glm::mat4 view{1.f};
    alignas(8) glm::ivec2 resolution;
    alignas(4) uint32_t frameIndex = 0;
    alignas(4) float time;
};

struct UnlitMaterialUBO
{
    alignas(16) glm::vec3 baseColor0 = glm::vec3(1.0f);
    alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
    alignas(4) float mixValue        = 0.5f;
    alignas(16) material::TextureParam textureParam0;
    alignas(16) material::TextureParam textureParam1;
};


struct UnlitMaterial : public Material
{
    UnlitMaterialUBO uMaterial;

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

    [[nodiscard]] UnlitMaterialUBO &getParams() { return uMaterial; }
};


struct UnlitMaterialComponent : public MaterialComponent<UnlitMaterial>
{
};

} // namespace ya