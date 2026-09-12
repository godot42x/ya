#include "Core/Os/Os.h"

#include "Core/Base.h"
#include "Core/KeyCode.h"

#if USE_SDL
    #include <SDL3/SDL.h>
    #include <SDL3/SDL_filesystem.h>
    #include <SDL3/SDL_keycode.h>
    #include <SDL3/SDL_loadso.h>
    #include <SDL3/SDL_mouse.h>
#endif

#include <chrono>
#include <cstdint>
#include <thread>

namespace ya::Os
{

std::string lastError()
{
#if USE_SDL
    const char* error = SDL_GetError();
    return error ? std::string(error) : std::string{};
#else
    return {};
#endif
}

std::filesystem::path executableBasePath()
{
#if USE_SDL
    if (const char* basePath = SDL_GetBasePath()) {
        return std::filesystem::path(basePath);
    }
#endif
    return {};
}

void sleepMs(uint32_t milliseconds)
{
#if USE_SDL
    SDL_Delay(milliseconds);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

bool openUrl(std::string_view url)
{
#if USE_SDL
    const std::string owned(url);
    return SDL_OpenURL(owned.c_str());
#else
    (void)url;
    return false;
#endif
}

uint32_t queryKeyModState()
{
#if USE_SDL
    return static_cast<uint32_t>(SDL_GetModState());
#else
    return 0;
#endif
}

SharedLibrary loadLibrary(const std::filesystem::path& path)
{
    SharedLibrary library;
#if USE_SDL
    library.handle = SDL_LoadObject(path.string().c_str());
#else
    (void)path;
#endif
    return library;
}

void* loadSymbol(SharedLibrary library, const char* name)
{
#if USE_SDL
    if (!library.handle || !name) {
        return nullptr;
    }
    const SDL_FunctionPointer symbol =
        SDL_LoadFunction(static_cast<SDL_SharedObject*>(library.handle), name);
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(symbol));
#else
    (void)library;
    (void)name;
    return nullptr;
#endif
}

void unloadLibrary(SharedLibrary& library)
{
#if USE_SDL
    if (library.handle) {
        SDL_UnloadObject(static_cast<SDL_SharedObject*>(library.handle));
        library.handle = nullptr;
    }
#else
    library.handle = nullptr;
#endif
}

std::string clipboardText()
{
#if USE_SDL
    char* text = SDL_GetClipboardText();
    if (!text) {
        return {};
    }
    std::string out(text);
    SDL_free(text);
    return out;
#else
    return {};
#endif
}

bool setClipboardText(std::string_view text)
{
#if USE_SDL
    const std::string owned(text);
    return SDL_SetClipboardText(owned.c_str());
#else
    (void)text;
    return false;
#endif
}

#if USE_SDL
namespace
{

SDL_DisplayID* lockDisplays(int& count)
{
    count = 0;
    return SDL_GetDisplays(&count);
}

} // namespace
#endif

int displayCount()
{
#if USE_SDL
    int count = 0;
    SDL_DisplayID* displays = lockDisplays(count);
    if (displays) {
        SDL_free(displays);
    }
    return count;
#else
    return 0;
#endif
}

std::string displayName(int index)
{
#if USE_SDL
    int count = 0;
    SDL_DisplayID* displays = lockDisplays(count);
    if (!displays || index < 0 || index >= count) {
        if (displays) {
            SDL_free(displays);
        }
        return {};
    }
    const char* name = SDL_GetDisplayName(displays[index]);
    std::string out  = name ? std::string(name) : std::string{};
    SDL_free(displays);
    return out;
#else
    (void)index;
    return {};
#endif
}

bool displayBounds(int index, int& x, int& y, int& w, int& h, bool usableWorkArea)
{
    x = 0;
    y = 0;
    w = 0;
    h = 0;
#if USE_SDL
    int count = 0;
    SDL_DisplayID* displays = lockDisplays(count);
    if (!displays || index < 0 || index >= count) {
        if (displays) {
            SDL_free(displays);
        }
        return false;
    }
    SDL_Rect rect{};
    bool     ok = false;
    if (usableWorkArea) {
        ok = SDL_GetDisplayUsableBounds(displays[index], &rect);
    }
    if (!ok || rect.w <= 0 || rect.h <= 0) {
        ok = SDL_GetDisplayBounds(displays[index], &rect);
    }
    SDL_free(displays);
    if (!ok || rect.w <= 0 || rect.h <= 0) {
        return false;
    }
    x = rect.x;
    y = rect.y;
    w = rect.w;
    h = rect.h;
    return true;
#else
    (void)index;
    (void)usableWorkArea;
    return false;
#endif
}

} // namespace ya::Os

#if USE_SDL
static_assert(static_cast<uint32_t>(ya::EKey::K_A) == SDLK_A);
static_assert(static_cast<uint32_t>(ya::EKey::Enter) == SDLK_RETURN);
static_assert(static_cast<uint32_t>(ya::EKey::Escape) == SDLK_ESCAPE);
static_assert(static_cast<uint32_t>(ya::EKey::F1) == SDLK_F1);
static_assert(static_cast<uint32_t>(ya::EKey::LCtrl) == SDLK_LCTRL);
static_assert(static_cast<uint32_t>(ya::EKey::LMeta) == SDLK_LGUI);
static_assert(static_cast<uint32_t>(ya::EKeyMod::LShift) == SDL_KMOD_LSHIFT);
static_assert(static_cast<uint32_t>(ya::EKeyMod::Ctrl) == SDL_KMOD_CTRL);
static_assert(static_cast<uint32_t>(ya::EKeyMod::Gui) == SDL_KMOD_GUI);
static_assert(static_cast<uint8_t>(ya::EMouse::Left) == SDL_BUTTON_LEFT);
static_assert(static_cast<uint8_t>(ya::EMouse::Right) == SDL_BUTTON_RIGHT);
#endif
