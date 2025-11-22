#pragma once

#include "Material.h"

namespace ya
{

struct LitMaterial : public Material
{

    struct MaterialUBO
    {
        alignas(16) glm::vec3 baseColor0 = glm::vec3(1.0f);
        alignas(16) glm::vec3 baseColor1 = glm::vec3(1.0f);
        alignas(4) float mixValue        = 0.5f;
        // alignas(16) TextureParam textureParam0;
        // alignas(16) TextureParam textureParam1;
    } uMaterial;



    bool bDirty = false;


  public:

    [[nodiscard]] MaterialUBO &getParams() { return uMaterial; }

    void               setDirty(bool bInDirty = true) { bDirty = bInDirty; }
    [[nodiscard]] bool isDirty() const { return bDirty; }
};

} // namespace ya