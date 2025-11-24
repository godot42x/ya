#pragma once

#include "Material.h"

namespace ya
{

struct LitMaterial : public Material
{
    struct ParamUBO
    {
        alignas(16) glm::vec3 objectColor = glm::vec3(1.0f);
        // alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
        // alignas(4) float mixValue        = 0.5f;
        // alignas(16) TextureParam textureParam0;
        // alignas(16) TextureParam textureParam1;
    } uParams;


    bool bParamDirty = false;


  public:

    [[nodiscard]] const ParamUBO &getParams() { return uParams; }
    ParamUBO                     &getParamsMut() { return uParams; }

    void               setParamDirty(bool bInDirty = true) { bParamDirty = bInDirty; }
    [[nodiscard]] bool isParamDirty() const { return bParamDirty; }

    void setObjectColor(const glm::vec3 &color)
    {
        uParams.objectColor = color;
        setParamDirty();
    }
};

} // namespace ya