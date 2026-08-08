
#pragma once


#include "Foundation/Core/Api.h"
#include "Foundation/Core/Delegate.h"
#include "Foundation/Core/Trait.h"
#include "Foundation/Core/TypeIndex.h"

#include <entt/fwd.hpp>


namespace ya
{


struct SceneBus : public disable_copy
{
    static YA_GAMEPLAY_ECS_API SceneBus& get();

    MulticastDelegate<void(entt::registry&, const entt::entity, ya::type_index_t)> onComponentRemoved;
};

} // namespace ya
