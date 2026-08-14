#pragma once

#include "Core/Api.h"
#include "RHI/NativeWindow.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ya
{

/// Owns the set of native windows created for one app runtime instance.
///
/// This is intentionally just the native-window lifecycle manager. GUI
/// activation order, multi-window routing policy, modal scope, drag across
/// windows, and presenter orchestration belong higher up in GUI/Host.
class YA_APP_RUNTIME_API NativeWindowManager
{
  public:
    NativeWindowManager() = default;
    ~NativeWindowManager();

    NativeWindowManager(const NativeWindowManager&)            = delete;
    NativeWindowManager& operator=(const NativeWindowManager&) = delete;

    bool init();
    void shutdown();

    INativeWindow* createWindow(const WindowCreateInfo& ci);
    INativeWindow* createMainWindow(const WindowCreateInfo& ci);

    INativeWindow* getWindow(uint32_t windowID) const;
    INativeWindow* getMainWindow() const;
    uint32_t         getMainWindowID() const { return _mainWindowID; }
    std::vector<INativeWindow*> getWindows() const;

    bool destroyWindow(uint32_t windowID);
    void clear();

    [[nodiscard]] bool isInitialized() const { return _initialized; }
    [[nodiscard]] size_t getWindowCount() const { return _windows.size(); }

  private:
    std::vector<std::unique_ptr<INativeWindow>> _windows;
    std::unordered_map<uint32_t, INativeWindow*> _windowByID;
    uint32_t _mainWindowID = 0;
    bool     _initialized  = false;

    INativeWindow* addWindow(std::unique_ptr<INativeWindow> window, bool bSetMainWindow);
};

} // namespace ya
