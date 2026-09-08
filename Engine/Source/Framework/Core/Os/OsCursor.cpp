#include "Core/Os/OsCursor.h"

#include "Core/Base.h"
#include "Core/Log.h"

#if USE_SDL
    #include <SDL3/SDL.h>
#endif

#include <optional>

namespace ya
{

namespace
{

#if USE_SDL

SDL_SystemCursor toSdlSystemCursor(ECursorType type)
{
    switch (type) {
    case ECursorType::IBeam: {
        return SDL_SYSTEM_CURSOR_TEXT;
    }
    case ECursorType::ResizeEastWest: {
        return SDL_SYSTEM_CURSOR_EW_RESIZE;
    }
    case ECursorType::ResizeNorthSouth: {
        return SDL_SYSTEM_CURSOR_NS_RESIZE;
    }
    case ECursorType::Arrow:
    default: {
        return SDL_SYSTEM_CURSOR_DEFAULT;
    }
    }
}

constexpr size_t kSystemCursorCount =
    static_cast<size_t>(ECursorType::ResizeNorthSouth) + 1;

struct FCursorCache
{
    SDL_Cursor*                handles[kSystemCursorCount]{};
    std::optional<ECursorType> active;

    SDL_Cursor* get(ECursorType type)
    {
        const size_t index = static_cast<size_t>(type);
        if (index >= kSystemCursorCount) {
            return handles[0];
        }
        if (!handles[index]) {
            handles[index] = SDL_CreateSystemCursor(toSdlSystemCursor(type));
            if (!handles[index]) {
                YA_CORE_ERROR("OsCursor: failed to create system cursor: {}", SDL_GetError());
            }
        }
        return handles[index];
    }
};

FCursorCache& cursorCache()
{
    static FCursorCache cache;
    return cache;
}

#endif

} // namespace

void OsCursor::set(ECursorType type)
{
#if USE_SDL
    FCursorCache& cache = cursorCache();
    if (cache.active == type) {
        return;
    }
    SDL_Cursor* handle = cache.get(type);
    if (!handle) {
        return;
    }
    cache.active = type;
    SDL_SetCursor(handle);
#else
    (void)type;
#endif
}

void OsCursor::show()
{
#if USE_SDL
    SDL_ShowCursor();
    cursorCache().active.reset();
#endif
}

void OsCursor::hide()
{
#if USE_SDL
    SDL_HideCursor();
    cursorCache().active.reset();
#endif
}

} // namespace ya
