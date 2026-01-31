
#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Common/FWD-std.h"
#include "ECS/Component.h"
#include "Render/Material/Material.h"


namespace ya
{


struct UIComponent : public IComponent
{
    YA_REFLECT_BEGIN(UIComponent, IComponent)
    YA_REFLECT_FIELD(width)
    YA_REFLECT_FIELD(height)
    YA_REFLECT_FIELD(view)
    YA_REFLECT_END()

    float       width  = 100.0f;
    float       height = 50.0f;
    TextureSlot view;
};

} // namespace ya