#pragma once
#include "ECS/Component.h"
#include "Core/Common/TextureSlot.h"


namespace ya
{

struct UnlitMaterial;

class YA_RENDER_3D_API BillboardComponent : public IComponent
{
    YA_REFLECT_BEGIN(BillboardComponent, IComponent)
    YA_REFLECT_FIELD(bVisible)
    YA_REFLECT_FIELD(image)
    YA_REFLECT_FIELD(tint, .color())
    YA_REFLECT_FIELD(worldDirection)
    YA_REFLECT_FIELD(screenSizePixels)
    YA_REFLECT_FIELD(minWorldScale)
    YA_REFLECT_END()


  public:
    BillboardComponent()
    {
        image.textureRef.onModified.addLambda(this, [this]() {
            invalidate();
        });
    }

    ~BillboardComponent() override;

    bool      bVisible          = true;
    TextureSlot image;
    glm::vec4  tint             = glm::vec4(1.0f);
    glm::vec3  worldDirection   = glm::vec3(0.0f, 0.0f, -1.0f);
    float      screenSizePixels = 30.0f;
    float      minWorldScale    = 0.0f;
    bool       bManagedByLight  = false;

    bool bDirty = true;
    void invalidate() { bDirty = true; }

    UnlitMaterial* getMaterial() const { return _material; }

    bool resolve();

  private:
    UnlitMaterial* _material = nullptr; ///< Owned through MaterialFactory (render module)
};
} // namespace ya
