#include "GUI/Host/NativeWindowManager.h"

#include "Core/Log.h"

#if USE_SDL
    #include <SDL3/SDL.h>
#endif

#include <algorithm>

namespace ya
{

NativeWindowManager::~NativeWindowManager()
{
    shutdown();
}

bool NativeWindowManager::init()
{
    _initialized = true;
    return true;
}

void NativeWindowManager::shutdown()
{
    if (!_initialized) {
        return;
    }

    clear();

#if USE_SDL
    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Quit();
    }
#endif

    _initialized = false;
}

INativeWindow* NativeWindowManager::createWindow(const WindowCreateInfo& ci)
{
    if (!_initialized && !init()) {
        return nullptr;
    }

#if USE_SDL
    auto window = std::make_unique<SDLNativeWindow>();
#else
    std::unique_ptr<INativeWindow> window;
#endif
    YA_CORE_ASSERT(window != nullptr, "No native window implementation available");
    YA_CORE_ASSERT(window->init(), "Failed to initialize native window");
    YA_CORE_ASSERT(window->recreate(ci), "Failed to recreate native window");
    return addWindow(std::move(window), false);
}

INativeWindow* NativeWindowManager::createMainWindow(const WindowCreateInfo& ci)
{
    INativeWindow* window = createWindow(ci);
    YA_CORE_ASSERT(window != nullptr, "Failed to create main window");
    _mainWindowID = window->getWindowID();
    return window;
}

INativeWindow* NativeWindowManager::getWindow(uint32_t windowID) const
{
    const auto it = _windowByID.find(windowID);
    return it != _windowByID.end() ? it->second : nullptr;
}

INativeWindow* NativeWindowManager::getMainWindow() const
{
    return getWindow(_mainWindowID);
}

std::vector<INativeWindow*> NativeWindowManager::getWindows() const
{
    std::vector<INativeWindow*> result;
    result.reserve(_windows.size());
    for (const auto& window : _windows) {
        result.push_back(window.get());
    }
    return result;
}

bool NativeWindowManager::destroyWindow(uint32_t windowID)
{
    const auto it = std::find_if(_windows.begin(),
                                 _windows.end(),
                                 [windowID](const std::unique_ptr<INativeWindow>& window)
                                 { return window && window->getWindowID() == windowID; });
    if (it == _windows.end()) {
        return false;
    }

    _windowByID.erase(windowID);
    if (_mainWindowID == windowID) {
        _mainWindowID = 0;
    }
    _windows.erase(it);

    if (_mainWindowID == 0 && !_windows.empty()) {
        _mainWindowID = _windows.front()->getWindowID();
    }

    return true;
}

void NativeWindowManager::clear()
{
    _windowByID.clear();
    _windows.clear();
    _mainWindowID = 0;
}

INativeWindow* NativeWindowManager::addWindow(std::unique_ptr<INativeWindow> window, bool bSetMainWindow)
{
    YA_CORE_ASSERT(window != nullptr, "Native window is null");

    INativeWindow* raw = window.get();
    _windows.push_back(std::move(window));

    if (bSetMainWindow && raw->getWindowID() != 0) {
        _mainWindowID = raw->getWindowID();
    }
    if (raw->getWindowID() != 0) {
        _windowByID[raw->getWindowID()] = raw;
    }
    return raw;
}

} // namespace ya
