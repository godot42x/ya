#include "Core/UI/DialogWindow.h"

#ifdef _WIN32
    #include "Platform/Window/WindowsDialogWindow.h"
#elif defined(__APPLE__)
// Include macOS implementation when available
#elif defined(__linux__)
// Include Linux implementation when available
#endif

namespace yaEngine
{

std::unique_ptr<DialogWindow> DialogWindow::create()
{
#ifdef _WIN32
    return std::make_unique<WindowsDialogWindow>();
#elif defined(__APPLE__)
    // Return macOS implementation
    // return std::make_unique<MacOSDialogWindow>();
    return nullptr;
#elif defined(__linux__)
    // Return Linux implementation
    // return std::make_unique<LinuxDialogWindow>();
    return nullptr;
#else
    return nullptr;
#endif
}

} // namespace yaEngine
