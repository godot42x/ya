
#pragma once

#include "Foundation/Core/Common/AssetRef.h"
#include "Foundation/Core/Common/FWD-std.h"
#include "Framework/Game/Gameplay/ECS/Component.h"
#include "Framework/Game/Render/Render3D/Material/Material.h"


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