
#pragma once

#include "utils/trait/disable_copy.hpp"

namespace neon {

class Window : public disable_copy
{
    Window() {}
    virtual ~Window() {}
};

} // namespace neon