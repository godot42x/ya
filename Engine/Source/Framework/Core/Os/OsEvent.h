#pragma once

#include "Core/Api.h"
#include "Core/Event.h"

#include <functional>

namespace ya
{

struct FOsMouseQuery
{
    uint32_t windowID   = 0;
    float    x          = 0.0f;
    float    y          = 0.0f;
    bool     bHasWindow = false;
};

/// Process-wide native event pump. SDL is an implementation detail; hosts
/// receive Core `Event` values and must not include SDL headers.
struct YA_CORE_API OsEventPump
{
    static void pump();
    static void poll(const std::function<void(const Event&)>& emit);
    [[nodiscard]] static FOsMouseQuery queryMouse();
};

} // namespace ya
