#include "AppRuntime/WindowManager.h"

#include "Core/Log.h"

#if USE_SDL
    #include <SDL3/SDL.h>
#endif

#include <algorithm>

namespace ya
{

WindowManager::~WindowManager()
{
    shutdown();
}

bool WindowManager::init()
{
    _initialized = true;
    return true;
}

void WindowManager::shutdown()
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

IWindowProvider* WindowManager::createWindow(const WindowCreateInfo& ci)
{
    if (!_initialized && !init()) {
        return nullptr;
    }

#if USE_SDL
    auto provider = std::make_unique<SDLWindowProvider>();
#else
    std::unique_ptr<IWindowProvider> provider;
#endif
    YA_CORE_ASSERT(provider != nullptr, "No window provider implementation available");
    YA_CORE_ASSERT(provider->init(), "Failed to initialize window provider");
    YA_CORE_ASSERT(provider->recreate(ci), "Failed to recreate window");
    return addWindow(std::move(provider), false);
}

IWindowProvider* WindowManager::createMainWindow(const WindowCreateInfo& ci)
{
    IWindowProvider* window = createWindow(ci);
    YA_CORE_ASSERT(window != nullptr, "Failed to create main window");
    _mainWindowID = window->getWindowID();
    return window;
}

IWindowProvider* WindowManager::getWindow(uint32_t windowID) const
{
    const auto it = _windowByID.find(windowID);
    return it != _windowByID.end() ? it->second : nullptr;
}

IWindowProvider* WindowManager::getMainWindow() const
{
    return getWindow(_mainWindowID);
}

std::vector<IWindowProvider*> WindowManager::getWindows() const
{
    std::vector<IWindowProvider*> result;
    result.reserve(_windows.size());
    for (const auto& window : _windows) {
        result.push_back(window.get());
    }
    return result;
}

bool WindowManager::destroyWindow(uint32_t windowID)
{
    const auto it = std::find_if(_windows.begin(),
                                 _windows.end(),
                                 [windowID](const std::unique_ptr<IWindowProvider>& window)
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

void WindowManager::clear()
{
    _windowByID.clear();
    _windows.clear();
    _mainWindowID = 0;
}

IWindowProvider* WindowManager::addWindow(std::unique_ptr<IWindowProvider> provider, bool bSetMainWindow)
{
    YA_CORE_ASSERT(provider != nullptr, "Window provider is null");

    IWindowProvider* raw = provider.get();
    _windows.push_back(std::move(provider));

    if (bSetMainWindow && raw->getWindowID() != 0) {
        _mainWindowID = raw->getWindowID();
    }
    if (raw->getWindowID() != 0) {
        _windowByID[raw->getWindowID()] = raw;
    }
    return raw;
}

} // namespace ya

