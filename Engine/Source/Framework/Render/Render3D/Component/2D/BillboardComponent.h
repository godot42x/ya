#pragma once
#include "ECS/Component.h"
#include "Core/Common/TextureSlot.h"
#include "Render3D/Common/RenderFeatures.h"


namespace ya
{

struct UnlitMaterial;

struct YA_RENDER_3D_API BillboardComponent : public IComponent
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

    /// Which views may draw this sprite (see RenderFeatures.h). Authored
    /// sprites are `Game`; a light icon generated as an editor companion is
    /// `Gizmo`. This is the sprite's own feature set -- which one a given view
    /// draws stays a view decision.
    FRenderFeatureMask features = toMask(ERenderFeature::Game);

    bool bDirty = true;
    void invalidate() { bDirty = true; }
    void onEdit() override { invalidate(); }

    UnlitMaterial* getMaterial() const { return _material; }

    bool resolve();

  private:
    UnlitMaterial* _material = nullptr; ///< Owned through MaterialFactory (render module)
};
} // namespace ya
