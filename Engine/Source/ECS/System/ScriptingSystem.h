
#pragma once

#include "Core/System/System.h"

namespace ya
{
struct ScriptingSystem : public GameInstanceSystem
{
    virtual void init() = 0;
};



} // namespace ya