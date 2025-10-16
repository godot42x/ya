#pragma once

#include <SDL3/SDL.h>

namespace ya
{

struct FPSControl
{
    float fps     = 0.0f;
    bool  bEnable = false;

    static constexpr float defaultFps = 60.f;

    float fpsLimit = defaultFps;
    float wantedDT = 1.f / defaultFps;

    static FPSControl *get();

    float update(float &dt)
    {
        if (!bEnable) {
            return 0;
        }

        if (dt < wantedDT)
        {
            float delayTimeSec = wantedDT - dt;
            // YA_CORE_INFO("FPS limit exceeded. Delaying for {} ms", delayTime);
            SDL_Delay(static_cast<Uint32>(delayTimeSec * 1000));
            return delayTimeSec;
        }

        return 0;
    }

    void setFPSLimit(float limit)
    {
        fpsLimit = limit;
        wantedDT = 1.f / fpsLimit;
    }
};

}; // namespace ya