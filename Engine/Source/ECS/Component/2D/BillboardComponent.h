#pragma once
#include "ECS/Component.h"
#include "Render/Material/Material.h"


namespace ya
{

class BillboardComponent : public IComponent
{
    YA_REFLECT_BEGIN(BillboardComponent, IComponent)
    YA_REFLECT_FIELD(image)
    YA_REFLECT_END()


  public:
    TextureSlot image;

    bool bDirty = true;
    void invalidate() { bDirty = true; }

    bool resolve();
};
} // namespace ya