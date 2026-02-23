#pragma once

#include "Core/Trait.h"

namespace ya
{

struct ConfigManager : public disable_copy
{
    static ConfigManager& get();
};


}; // namespace ya
