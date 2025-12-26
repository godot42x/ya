
#pragma once

#include "Core/System/System.h"

namespace ya
{
struct ScriptingSystem : public ISystem
{
    virtual void init() = 0;
};



} // namespace ya