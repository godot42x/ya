#pragma once
#include "ECS/Component.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Material/UnlitMaterial.h"
#include "Render/Material/Material.h"
#include "Runtime/Rendering/Common/RenderOverlay.h"


namespace ya
{

class BillboardComponent : public IComponent
{
    YA_REFLECT_BEGIN(BillboardComponent, IComponent)
    YA_REFLECT_FIELD(bVisible)
    YA_REFLECT_FIELD(image)
    YA_REFLECT_FIELD(tint, .color())
    YA_REFLECT_FIELD(worldDirection)
    YA_REFLECT_FIELD(screenSizePixels)
    YA_REFLECT_FIELD(minWorldScale)
    YA_REFLECT_FIELD(renderMode)
    YA_REFLECT_END()


  public:
    BillboardComponent()
    {
        image.textureRef.onModified.addLambda(this, [this]() {
            invalidate();
        });
    }

    ~BillboardComponent() override
    {
        if (_material) {
            if (auto* factory = MaterialFactory::get()) {
                factory->destroyMaterial(_material);
            }
            _material = nullptr;
        }
    }

    bool      bVisible          = true;
    TextureSlot image;
    glm::vec4  tint             = glm::vec4(1.0f);
    glm::vec3  worldDirection   = glm::vec3(0.0f, 0.0f, -1.0f);
    float      screenSizePixels = 30.0f;
    float      minWorldScale    = 0.0f;
    uint32_t   renderMode       = static_cast<uint32_t>(RenderOverlaySprite3D::Mode::Textured);
    bool       bManagedByLight  = false;

    bool bDirty = true;
    void invalidate() { bDirty = true; }

    UnlitMaterial* getMaterial() const { return _material; }

    [[nodiscard]] RenderOverlaySprite3D::Mode getMode() const
    {
        return static_cast<RenderOverlaySprite3D::Mode>(renderMode);
    }

    void setMode(RenderOverlaySprite3D::Mode mode)
    {
        renderMode = static_cast<uint32_t>(mode);
        invalidate();
    }

    bool resolve();

  private:
    UnlitMaterial* _material = nullptr;
};
} // namespace ya
