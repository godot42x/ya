
#pragma once


#include "Core/Api.h"
#include "Core/Delegate.h"
#include "Core/Trait.h"
#include "Core/TypeIndex.h"

#include <entt/fwd.hpp>


namespace ya
{


struct ENGINE_API SceneBus : public disable_copy
{
    static SceneBus& get();

    MulticastDelegate<void(entt::registry&, const entt::entity, ya::type_index_t)> onComponentRemoved;
};

} // namespace ya
