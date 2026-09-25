#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"

#include <vector>

namespace ya
{

struct INativeWindow;

/// Capability-driven native window chrome. Dock / EditorSurface / tab spawners
/// consume this API only; they must not call NSWindow or Win32 non-client APIs.
enum class EWindowChromeMode : uint8_t
{
    Native,
    Hybrid,
    ClientDrawn
};

enum class EWindowChromeHit : uint8_t
{
    Client,
    Drag,
    SystemButton,
    ResizeN,
    ResizeS,
    ResizeE,
    ResizeW,
    ResizeNE,
    ResizeNW,
    ResizeSE,
    ResizeSW
};

struct FWindowChromeCapabilities
{
    bool nativeDecorations  = true;
    bool hybridTitleContent = false;
    bool clientDrawn        = false;
    bool customTitleLayout  = false;
    bool dragRegion         = false;
    bool resizeHitTest      = false;
    bool systemButtons      = true;
    bool safeAreaInsets     = false;
    bool shadow             = true;
    bool fullscreenMaximize = true;
    bool accessibility      = true;
};

struct FWindowChromeInsets
{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
};

struct FWindowChromeRect
{
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const
    {
        return px >= x && py >= y && px < (x + width) && py < (y + height);
    }

    [[nodiscard]] bool empty() const { return width <= 0.0f || height <= 0.0f; }
};

struct FWindowChromeLayout
{
    EWindowChromeMode   mode = EWindowChromeMode::Native;
    FWindowChromeInsets contentInsets;
    FWindowChromeRect   titleContent;
    FWindowChromeRect   dragRegion;
    FWindowChromeRect   systemButtons;
    /// Interactive chrome inside the title band (the page TabBar rect, not
    /// only individual tab buttons). Empty gutter outside this rect is Drag
    /// so the window can still be moved. Tab drag must stay Client or the
    /// OS caption path steals the gesture and the whole window follows.
    std::vector<FWindowChromeRect> titleClientHits;
    float               windowWidth  = 0.0f;
    float               windowHeight = 0.0f;
    float               resizeBorder = 0.0f;
    bool                bResizable   = true;
};

struct FWindowChromeState
{
    EWindowChromeMode         mode = EWindowChromeMode::Native;
    FWindowChromeCapabilities capabilities;
    FWindowChromeInsets       safeArea;
    FWindowChromeLayout       layout;
};

[[nodiscard]] YA_GUI_API FWindowChromeCapabilities queryWindowChromeCapabilities();
/// Chrome for a window whose consumer did not choose: standard OS
/// decorations. Transparent title bars (Hybrid) are an explicit downstream
/// choice, not a platform default.
[[nodiscard]] YA_GUI_API EWindowChromeMode         defaultWindowChromeMode();
[[nodiscard]] YA_GUI_API EWindowChromeMode         resolveWindowChromeMode(EWindowChromeMode requested);

[[nodiscard]] YA_GUI_API FWindowChromeInsets queryWindowChromeInsets(INativeWindow& window);

/// Consumer API: resolved mode + layout floors (Hybrid traffic-light min size,
/// titlebar min height, trailing drag gutter). Prefer this over raw safe-area
/// insets so menus sit below the title band, not under system buttons.
/// Empty gutter is Drag (move); register the title TabBar rect with
/// `updateWindowChromeTitleClientHits` so tabs stay Client. Double-click zoom
/// is `handleWindowChromeTitleDoubleClick`, not native title-bar zoom.
[[nodiscard]] YA_GUI_API FWindowChromeLayout queryWindowChromeLayout(INativeWindow&    window,
                                                                    EWindowChromeMode requested,
                                                                    bool              bResizable = true);

[[nodiscard]] YA_GUI_API FWindowChromeLayout makeWindowChromeLayout(EWindowChromeMode                 mode,
                                                                    const FWindowChromeCapabilities&  caps,
                                                                    Extent2D                          windowSize,
                                                                    FWindowChromeInsets               safeArea,
                                                                    bool                              bResizable);

[[nodiscard]] YA_GUI_API EWindowChromeHit classifyWindowChromeHit(const FWindowChromeLayout& layout,
                                                                  float                      x,
                                                                  float                      y);

/// Apply resolved chrome to an existing native window (`INativeWindow` +
/// platform backend). GameEditor must not call NSWindow/Win32 APIs itself.
YA_GUI_API FWindowChromeState applyWindowChrome(INativeWindow&     window,
                                                EWindowChromeMode  requested,
                                                bool               bResizable = true);
YA_GUI_API void                clearWindowChrome(INativeWindow& window);
/// Publish interactive holes in the title band (the page TabBar). Trailing
/// gutter remains Drag so the window still moves; zoom is
/// `handleWindowChromeTitleDoubleClick`.
YA_GUI_API void updateWindowChromeTitleClientHits(INativeWindow&                        window,
                                                  const std::vector<FWindowChromeRect>& hits);

/// Hit class for the live stored Hybrid/ClientDrawn layout (window
/// coordinates, top-left origin). Client when no layout is stored.
[[nodiscard]] YA_GUI_API EWindowChromeHit queryStoredWindowChromeHit(INativeWindow& window,
                                                                     float          x,
                                                                     float          y);

/// Toggle zoom/maximize the way a native title-bar double-click does.
/// macOS uses AppKit `zoom:` / the user's "Double-click a window's title
/// bar" preference; other platforms toggle `INativeWindow` maximize.
YA_GUI_API bool toggleWindowChromeTitleZoom(INativeWindow& window);

/// If `(x,y)` classifies as Drag, run `toggleWindowChromeTitleZoom`.
/// Traffic lights and title Client holes are ignored.
YA_GUI_API bool handleWindowChromeTitleDoubleClick(INativeWindow& window, float x, float y);

/// Click-through for host-owned overlay windows (drag ghost). GameEditor
/// must not call NSWindow/Win32 APIs itself.
YA_GUI_API bool applyNativeClickThrough(INativeWindow& window, bool enable);

} // namespace ya
