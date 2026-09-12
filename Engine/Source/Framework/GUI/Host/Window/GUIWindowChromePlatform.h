#pragma once

namespace ya
{

#if defined(__APPLE__)
bool applyMacOsHybridChrome(void* nativeWindowHandle);
bool applyMacOsNativeChrome(void* nativeWindowHandle);
bool applyMacOsClickThrough(void* nativeWindowHandle, bool enable);
bool installMacOsTitleDoubleClickMonitor(void* nativeWindowHandle);
void removeMacOsTitleDoubleClickMonitor(void* nativeWindowHandle);
bool performMacOsTitlebarDoubleClick(void* nativeWindowHandle);
#else
inline bool applyMacOsHybridChrome(void*) { return false; }
inline bool applyMacOsNativeChrome(void*) { return false; }
inline bool applyMacOsClickThrough(void*, bool) { return false; }
inline bool installMacOsTitleDoubleClickMonitor(void*) { return false; }
inline void removeMacOsTitleDoubleClickMonitor(void*) {}
inline bool performMacOsTitlebarDoubleClick(void*) { return false; }
#endif

/// Native-handle hit lookup (top-left). Cocoa-safe: do not include
/// GUIWindowChrome.h from .mm (AppKit `Class` vs reflects `Class`).
bool isStoredNativeWindowChromeHitDrag(void* nativeWindowHandle, float x, float y);

} // namespace ya
