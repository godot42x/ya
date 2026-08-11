#pragma once

#include "Core/Api.h"
#include "RHI/WindowProvider.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ya
{

class YA_APP_RUNTIME_API WindowManager
{
  public:
    WindowManager() = default;
    ~WindowManager();

    WindowManager(const WindowManager&)            = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    bool init();
    void shutdown();

    IWindowProvider* createWindow(const WindowCreateInfo& ci);
    IWindowProvider* createMainWindow(const WindowCreateInfo& ci);

    IWindowProvider* getWindow(uint32_t windowID) const;
    IWindowProvider* getMainWindow() const;
    uint32_t         getMainWindowID() const { return _mainWindowID; }
    std::vector<IWindowProvider*> getWindows() const;

    bool destroyWindow(uint32_t windowID);
    void clear();

    [[nodiscard]] bool isInitialized() const { return _initialized; }
    [[nodiscard]] size_t getWindowCount() const { return _windows.size(); }

  private:
    std::vector<std::unique_ptr<IWindowProvider>> _windows;
    std::unordered_map<uint32_t, IWindowProvider*> _windowByID;
    uint32_t _mainWindowID = 0;
    bool     _initialized  = false;

    IWindowProvider* addWindow(std::unique_ptr<IWindowProvider> provider, bool bSetMainWindow);
};

} // namespace ya

