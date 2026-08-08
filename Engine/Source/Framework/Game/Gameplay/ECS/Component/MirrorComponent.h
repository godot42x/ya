#pragma once


#include "Foundation/Core/Base.h"
#include "Framework/Game/Gameplay/ECS/Component.h"


namespace ya
{

struct MirrorComponent : public IComponent
{
    YA_REFLECT_BEGIN(MirrorComponent, IComponent)
    YA_REFLECT_END()
};

} // namespace ya