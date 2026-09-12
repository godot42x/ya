#include "GUI/Host/GUIWindowChrome.h"

#include "GUIWindowChromePlatform.h"
#include "RHI/NativeWindow.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ya
{
namespace
{

constexpr float kHybridTitlebarMinHeight     = 28.0f;
constexpr float kHybridTrafficLightsMinWidth = 78.0f;
constexpr float kTitleDragGutterWidth        = 96.0f;
constexpr float kClientDrawnTitlebarHeight   = 36.0f;
constexpr float kClientDrawnResizeBorder     = 6.0f;

std::mutex                                              g_hitLayoutMutex;
std::unordered_map<void*, std::unique_ptr<FWindowChromeLayout>> g_hitLayouts;

[[nodiscard]] void* nativeHandleOf(INativeWindow& window)
{
    return window.getNativeWindowHandle();
}

[[nodiscard]] Extent2D nativeWindowExtent(INativeWindow& window)
{
    int width  = 1;
    int height = 1;
    window.getWindowSize(width, height);
    return Extent2D{
        .width  = static_cast<uint32_t>(std::max(width, 1)),
        .height = static_cast<uint32_t>(std::max(height, 1)),
    };
}

[[nodiscard]] ENativeWindowHitResult toNativeHitResult(EWindowChromeHit hit)
{
    switch (hit) {
    case EWindowChromeHit::Drag: {
        // Cocoa maps Draggable to [NSWindow setMovableByWindowBackground:]
        // for the whole window, not per-pixel. Hovering Hybrid empty title then
        // eats the next client click (first press starts a move or is dropped
        // when leaving the drag area). Title drag stays on the Cocoa local
        // monitor via performWindowDragWithEvent.
#if defined(__APPLE__)
        return ENativeWindowHitResult::Normal;
#else
        return ENativeWindowHitResult::Draggable;
#endif
    }
    case EWindowChromeHit::ResizeN: {
        return ENativeWindowHitResult::ResizeTop;
    }
    case EWindowChromeHit::ResizeS: {
        return ENativeWindowHitResult::ResizeBottom;
    }
    case EWindowChromeHit::ResizeE: {
        return ENativeWindowHitResult::ResizeRight;
    }
    case EWindowChromeHit::ResizeW: {
        return ENativeWindowHitResult::ResizeLeft;
    }
    case EWindowChromeHit::ResizeNE: {
        return ENativeWindowHitResult::ResizeTopRight;
    }
    case EWindowChromeHit::ResizeNW: {
        return ENativeWindowHitResult::ResizeTopLeft;
    }
    case EWindowChromeHit::ResizeSE: {
        return ENativeWindowHitResult::ResizeBottomRight;
    }
    case EWindowChromeHit::ResizeSW: {
        return ENativeWindowHitResult::ResizeBottomLeft;
    }
    case EWindowChromeHit::Client:
    case EWindowChromeHit::SystemButton: {
        return ENativeWindowHitResult::Normal;
    }
    }
    return ENativeWindowHitResult::Normal;
}

[[nodiscard]] bool copyStoredHitLayout(void* nativeWindowHandle, FWindowChromeLayout& out)
{
    if (!nativeWindowHandle) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_hitLayoutMutex);
    const auto it = g_hitLayouts.find(nativeWindowHandle);
    if (it == g_hitLayouts.end() || !it->second) {
        return false;
    }
    out = *it->second;
    return true;
}

ENativeWindowHitResult chromeHitCallback(void* userdata, float x, float y)
{
    FWindowChromeLayout layout;
    if (!copyStoredHitLayout(userdata, layout)) {
        return ENativeWindowHitResult::Normal;
    }
    return toNativeHitResult(classifyWindowChromeHit(layout, x, y));
}

void storeHitLayout(INativeWindow& window, const FWindowChromeLayout& layout)
{
    void* handle = nativeHandleOf(window);
    if (!handle) {
        return;
    }
    {
        auto owned = std::make_unique<FWindowChromeLayout>(layout);
        std::lock_guard<std::mutex> lock(g_hitLayoutMutex);
        g_hitLayouts[handle] = std::move(owned);
    }
    (void)window.setHitTest(&chromeHitCallback, handle);
    (void)installMacOsTitleDoubleClickMonitor(handle);
}

void eraseHitLayout(INativeWindow& window)
{
    void* handle = nativeHandleOf(window);
    if (!handle) {
        return;
    }
    (void)window.setHitTest(nullptr, nullptr);
    removeMacOsTitleDoubleClickMonitor(handle);
    std::lock_guard<std::mutex> lock(g_hitLayoutMutex);
    g_hitLayouts.erase(handle);
}

} // namespace

FWindowChromeCapabilities queryWindowChromeCapabilities()
{
    FWindowChromeCapabilities caps;
    caps.nativeDecorations  = true;
    caps.fullscreenMaximize = true;
    caps.accessibility      = true;
    caps.shadow             = true;
    caps.clientDrawn        = true;
    caps.customTitleLayout  = true;
    caps.dragRegion         = true;
    caps.resizeHitTest      = true;
    caps.safeAreaInsets     = true;
#if defined(__APPLE__)
    caps.hybridTitleContent = true;
    caps.systemButtons      = true;
#elif defined(_WIN32)
    caps.hybridTitleContent = false;
    caps.systemButtons      = true;
#else
    caps.hybridTitleContent = false;
    caps.systemButtons      = true;
#endif
    return caps;
}

EWindowChromeMode defaultWindowChromeMode()
{
#if defined(__APPLE__)
    return EWindowChromeMode::Hybrid;
#else
    return EWindowChromeMode::Native;
#endif
}

EWindowChromeMode resolveWindowChromeMode(EWindowChromeMode requested)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    switch (requested) {
    case EWindowChromeMode::Native: {
        if (caps.nativeDecorations) {
            return EWindowChromeMode::Native;
        }
        if (caps.hybridTitleContent) {
            return EWindowChromeMode::Hybrid;
        }
        return caps.clientDrawn ? EWindowChromeMode::ClientDrawn : EWindowChromeMode::Native;
    }
    case EWindowChromeMode::Hybrid: {
        if (caps.hybridTitleContent) {
            return EWindowChromeMode::Hybrid;
        }
        return EWindowChromeMode::Native;
    }
    case EWindowChromeMode::ClientDrawn: {
        if (caps.clientDrawn) {
            return EWindowChromeMode::ClientDrawn;
        }
        if (caps.hybridTitleContent) {
            return EWindowChromeMode::Hybrid;
        }
        return EWindowChromeMode::Native;
    }
    }
    return defaultWindowChromeMode();
}

FWindowChromeInsets queryWindowChromeInsets(INativeWindow& window)
{
    const NativeWindowSafeArea safe = window.getSafeArea();
    if (!safe.valid) {
        return {};
    }
    int width  = 0;
    int height = 0;
    window.getWindowSize(width, height);
    width  = std::max(width, 1);
    height = std::max(height, 1);
    FWindowChromeInsets insets;
    insets.left   = static_cast<float>(std::max(safe.x, 0));
    insets.top    = static_cast<float>(std::max(safe.y, 0));
    insets.right  = static_cast<float>(std::max(width - (safe.x + safe.w), 0));
    insets.bottom = static_cast<float>(std::max(height - (safe.y + safe.h), 0));
    return insets;
}

FWindowChromeLayout queryWindowChromeLayout(INativeWindow&    window,
                                            EWindowChromeMode requested,
                                            bool              bResizable)
{
    const FWindowChromeCapabilities caps = queryWindowChromeCapabilities();
    return makeWindowChromeLayout(resolveWindowChromeMode(requested),
                                  caps,
                                  nativeWindowExtent(window),
                                  queryWindowChromeInsets(window),
                                  bResizable);
}

FWindowChromeLayout makeWindowChromeLayout(EWindowChromeMode                 mode,
                                           const FWindowChromeCapabilities&  caps,
                                           Extent2D                          windowSize,
                                           FWindowChromeInsets               safeArea,
                                           bool                              bResizable)
{
    FWindowChromeLayout layout;
    layout.mode          = mode;
    layout.bResizable    = bResizable;
    const float width    = static_cast<float>(std::max(windowSize.width, 1u));
    const float height   = static_cast<float>(std::max(windowSize.height, 1u));
    layout.windowWidth   = width;
    layout.windowHeight  = height;

    switch (mode) {
    case EWindowChromeMode::Native: {
        layout.contentInsets = {};
        break;
    }
    case EWindowChromeMode::Hybrid: {
        const float titleH    = std::max(safeArea.top, kHybridTitlebarMinHeight);
        const float minButtonsW = caps.systemButtons ? kHybridTrafficLightsMinWidth : 0.0f;
        const float buttonsW  = std::max(safeArea.left, minButtonsW);
        const float remaining = std::max(width - buttonsW - safeArea.right, 0.0f);
        const float gutter    = caps.dragRegion ? std::min(kTitleDragGutterWidth, remaining) : 0.0f;
        const float clientW   = std::max(remaining - gutter, 0.0f);
        layout.contentInsets      = safeArea;
        layout.contentInsets.left = buttonsW;
        layout.contentInsets.top  = titleH;
        layout.systemButtons      = FWindowChromeRect{0.0f, 0.0f, buttonsW, titleH};
        layout.titleContent       = FWindowChromeRect{buttonsW, 0.0f, clientW, titleH};
        layout.dragRegion         = caps.dragRegion
                                        ? FWindowChromeRect{buttonsW + clientW, 0.0f, gutter, titleH}
                                        : FWindowChromeRect{};
        layout.resizeBorder       = 0.0f;
        break;
    }
    case EWindowChromeMode::ClientDrawn: {
        const float titleH  = std::max(safeArea.top, kClientDrawnTitlebarHeight);
        const float gutter  = caps.dragRegion ? std::min(kTitleDragGutterWidth, width) : 0.0f;
        const float clientW = std::max(width - gutter, 0.0f);
        layout.contentInsets     = FWindowChromeInsets{0.0f, titleH, 0.0f, 0.0f};
        layout.titleContent      = FWindowChromeRect{0.0f, 0.0f, clientW, titleH};
        layout.dragRegion        = caps.dragRegion
                                       ? FWindowChromeRect{clientW, 0.0f, gutter, titleH}
                                       : FWindowChromeRect{};
        layout.resizeBorder      = (bResizable && caps.resizeHitTest) ? kClientDrawnResizeBorder : 0.0f;
        layout.systemButtons     = {};
        break;
    }
    }
    return layout;
}

EWindowChromeHit classifyWindowChromeHit(const FWindowChromeLayout& layout, float x, float y)
{
    const float border = layout.resizeBorder;
    if (border > 0.0f && layout.bResizable) {
        const float width  = std::max(layout.windowWidth, border * 2.0f + 1.0f);
        const float height = std::max(layout.windowHeight, border * 2.0f + 1.0f);
        const bool  left   = x < border;
        const bool  right  = x >= (width - border);
        const bool  top    = y < border;
        const bool  bottom = y >= (height - border);
        if (top && left) {
            return EWindowChromeHit::ResizeNW;
        }
        if (top && right) {
            return EWindowChromeHit::ResizeNE;
        }
        if (bottom && left) {
            return EWindowChromeHit::ResizeSW;
        }
        if (bottom && right) {
            return EWindowChromeHit::ResizeSE;
        }
        if (top) {
            return EWindowChromeHit::ResizeN;
        }
        if (bottom) {
            return EWindowChromeHit::ResizeS;
        }
        if (left) {
            return EWindowChromeHit::ResizeW;
        }
        if (right) {
            return EWindowChromeHit::ResizeE;
        }
    }

    if (!layout.systemButtons.empty() && layout.systemButtons.contains(x, y)) {
        return EWindowChromeHit::SystemButton;
    }
    if (layout.contentInsets.top > 0.0f && y >= 0.0f && y < layout.contentInsets.top) {
        for (const FWindowChromeRect& hole : layout.titleClientHits) {
            if (hole.contains(x, y)) {
                return EWindowChromeHit::Client;
            }
        }
        return EWindowChromeHit::Drag;
    }
    if (!layout.dragRegion.empty() && layout.dragRegion.contains(x, y)) {
        return EWindowChromeHit::Drag;
    }
    return EWindowChromeHit::Client;
}

FWindowChromeState applyWindowChrome(INativeWindow& window, EWindowChromeMode requested, bool bResizable)
{
    FWindowChromeState state;
    state.capabilities = queryWindowChromeCapabilities();
    state.mode         = resolveWindowChromeMode(requested);

    void* handle = nativeHandleOf(window);
    if (!handle) {
        state.layout = makeWindowChromeLayout(state.mode, state.capabilities, Extent2D{1, 1}, {}, bResizable);
        return state;
    }

    switch (state.mode) {
    case EWindowChromeMode::Native: {
        (void)applyMacOsNativeChrome(handle);
        (void)window.setBordered(true);
        eraseHitLayout(window);
        break;
    }
    case EWindowChromeMode::Hybrid: {
        (void)window.setBordered(true);
        (void)applyMacOsHybridChrome(handle);
        break;
    }
    case EWindowChromeMode::ClientDrawn: {
        (void)applyMacOsNativeChrome(handle);
        (void)window.setBordered(false);
        break;
    }
    }

    state.safeArea = queryWindowChromeInsets(window);
    state.layout   = makeWindowChromeLayout(state.mode,
                                          state.capabilities,
                                          nativeWindowExtent(window),
                                          state.safeArea,
                                          bResizable);

    if (state.mode == EWindowChromeMode::Hybrid || state.mode == EWindowChromeMode::ClientDrawn) {
        storeHitLayout(window, state.layout);
    }
    return state;
}

void clearWindowChrome(INativeWindow& window)
{
    eraseHitLayout(window);
}

void updateWindowChromeTitleClientHits(INativeWindow& window, const std::vector<FWindowChromeRect>& hits)
{
    void* handle = nativeHandleOf(window);
    if (!handle) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_hitLayoutMutex);
    const auto it = g_hitLayouts.find(handle);
    if (it == g_hitLayouts.end() || !it->second) {
        return;
    }
    it->second->titleClientHits = hits;
}

namespace
{

EWindowChromeHit queryStoredNativeWindowChromeHit(void* nativeWindowHandle, float x, float y)
{
    FWindowChromeLayout layout;
    if (!copyStoredHitLayout(nativeWindowHandle, layout)) {
        return EWindowChromeHit::Client;
    }
    return classifyWindowChromeHit(layout, x, y);
}

} // namespace

bool isStoredNativeWindowChromeHitDrag(void* nativeWindowHandle, float x, float y)
{
    return queryStoredNativeWindowChromeHit(nativeWindowHandle, x, y) == EWindowChromeHit::Drag;
}

EWindowChromeHit queryStoredWindowChromeHit(INativeWindow& window, float x, float y)
{
    return queryStoredNativeWindowChromeHit(nativeHandleOf(window), x, y);
}

bool toggleWindowChromeTitleZoom(INativeWindow& window)
{
    if (performMacOsTitlebarDoubleClick(nativeHandleOf(window))) {
        return true;
    }
    if (window.isMaximized()) {
        return window.restoreFromMaximize();
    }
    return window.maximize();
}

bool handleWindowChromeTitleDoubleClick(INativeWindow& window, float x, float y)
{
#if defined(_WIN32)
    // HTCAPTION already performs title-bar double-click maximize.
    (void)window;
    (void)x;
    (void)y;
    return false;
#else
    if (queryStoredWindowChromeHit(window, x, y) != EWindowChromeHit::Drag) {
        return false;
    }
    return toggleWindowChromeTitleZoom(window);
#endif
}

bool applyNativeClickThrough(INativeWindow& window, bool enable)
{
    void* handle = nativeHandleOf(window);
    if (!handle) {
        return false;
    }
    const bool passthrough = window.setMousePassthrough(enable);
    const bool platform    = applyMacOsClickThrough(handle, enable);
    return passthrough || platform;
}

} // namespace ya
