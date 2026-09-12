#include "GUIWindowChromePlatform.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <SDL3/SDL.h>

namespace ya
{
namespace
{

char kYaTitleClickMonitorKey;

NSWindow* nsWindowFromSdl(void* sdlWindowHandle)
{
    auto* window = static_cast<SDL_Window*>(sdlWindowHandle);
    if (!window) {
        return nil;
    }
    return (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                      SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
                                                      nullptr);
}

SDL_Window* sdlWindowFromNs(NSWindow* nsWindow)
{
    if (!nsWindow) {
        return nullptr;
    }
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (!windows) {
        return nullptr;
    }
    SDL_Window* found = nullptr;
    for (int i = 0; i < count; ++i) {
        NSWindow* candidate = (__bridge NSWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(windows[i]),
            SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
            nullptr);
        if (candidate == nsWindow) {
            found = windows[i];
            break;
        }
    }
    SDL_free(windows);
    return found;
}

void topLeftContentPoint(NSWindow* nsWindow, NSEvent* event, float& x, float& y)
{
    NSView* content = nsWindow.contentView;
    NSPoint inView  = [content convertPoint:event.locationInWindow fromView:nil];
    const NSRect bounds = content.bounds;
    x = static_cast<float>(inView.x);
    y = static_cast<float>(bounds.size.height - inView.y);
}

} // namespace

bool applyMacOsHybridChrome(void* sdlWindowHandle)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return false;
    }
    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility            = NSWindowTitleHidden;
    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    return true;
}

bool applyMacOsNativeChrome(void* sdlWindowHandle)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return false;
    }
    nsWindow.titlebarAppearsTransparent = NO;
    nsWindow.titleVisibility            = NSWindowTitleVisible;
    nsWindow.styleMask &= ~static_cast<NSWindowStyleMask>(NSWindowStyleMaskFullSizeContentView);
    return true;
}

bool applyMacOsClickThrough(void* sdlWindowHandle, bool enable)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return false;
    }
    nsWindow.ignoresMouseEvents = enable ? YES : NO;
    return true;
}

bool performMacOsTitlebarDoubleClick(void* sdlWindowHandle)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return false;
    }
    NSString* action = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqualToString:@"None"]) {
        return true;
    }
    if ([action isEqualToString:@"Minimize"]) {
        [nsWindow miniaturize:nil];
        return true;
    }
    [nsWindow zoom:nil];
    return true;
}

void removeMacOsTitleDoubleClickMonitor(void* sdlWindowHandle)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return;
    }
    id monitor = objc_getAssociatedObject(nsWindow, &kYaTitleClickMonitorKey);
    if (monitor) {
        [NSEvent removeMonitor:monitor];
        objc_setAssociatedObject(nsWindow, &kYaTitleClickMonitorKey, nil, OBJC_ASSOCIATION_ASSIGN);
    }
}

bool installMacOsTitleDoubleClickMonitor(void* sdlWindowHandle)
{
    NSWindow* nsWindow = nsWindowFromSdl(sdlWindowHandle);
    if (!nsWindow) {
        return false;
    }
    removeMacOsTitleDoubleClickMonitor(sdlWindowHandle);
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                       handler:^NSEvent*(NSEvent* event) {
        if (event.window != nsWindow) {
            return event;
        }
        SDL_Window* sdl = sdlWindowFromNs(nsWindow);
        if (!sdl) {
            return event;
        }
        float x = 0.0f;
        float y = 0.0f;
        topLeftContentPoint(nsWindow, event, x, y);
        if (!isStoredNativeWindowChromeHitDrag(sdl, x, y)) {
            return event;
        }
        if (event.clickCount >= 2) {
            (void)performMacOsTitlebarDoubleClick(sdl);
            return nil;
        }
        if (event.clickCount == 1) {
            [nsWindow performWindowDragWithEvent:event];
            return nil;
        }
        return event;
    }];
    objc_setAssociatedObject(nsWindow, &kYaTitleClickMonitorKey, monitor, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return monitor != nil;
}

} // namespace ya
