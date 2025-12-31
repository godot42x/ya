#include "ReflectionSystem.h"

#include "Core/Log.h"

namespace ya
{

ReflectionSystem *ReflectionSystem::get()
{
    static ReflectionSystem instance;
    return &instance;
}

} // namespace ya
