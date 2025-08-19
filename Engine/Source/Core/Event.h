#pragma once

#include "SDL3/SDL_events.h"
struct EventProcessState
{
    enum EResult
    {
        Handled = 0,
        Continue,
        ENUM_MAX
    };

    EResult result = Handled;

    EventProcessState(EResult result) : result(result) {}

    operator bool() const
    {
        return result == Handled;
    }

    bool operator==(EResult rhs) const
    {
        return result == rhs;
    }

}; // namespace EventProcessState

using Event = SDL_Event;