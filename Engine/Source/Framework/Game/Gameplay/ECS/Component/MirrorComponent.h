#pragma once


#include "Core/Base.h"
#include "ECS/Component.h"


namespace ya
{

struct MirrorComponent : public IComponent
{
    YA_REFLECT_BEGIN(MirrorComponent, IComponent)
    YA_REFLECT_END()
};

} // namespace ya