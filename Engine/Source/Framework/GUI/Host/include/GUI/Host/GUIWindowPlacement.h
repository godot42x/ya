#pragma once

#include "Core/Api.h"
#include "Core/Common/Types.h"

#include <string>

namespace ya
{

struct INativeWindow;

/// Screen placement of a native window. Editor persists this; Dock must not
/// treat overlay Popup coordinates as these values.
struct FWindowScreenPlacement
{
    int         x             = 0;
    int         y             = 0;
    int         w             = 0;
    int         h             = 0;
    int         monitorIndex  = -1;
    std::string monitorName;
    bool        bMaximized    = false;
    bool        bMinimized    = false;
    bool        bHasOrigin    = false;
};

enum class EWindowPlacementRecovery : uint8_t
{
    Applied,    ///< origin and size as persisted
    Relocated,  ///< monitor gone or remapped; moved onto a live display
    SizeOnly,   ///< no persisted origin
};

struct FWindowPlacementApplyResult
{
    EWindowPlacementRecovery recovery     = EWindowPlacementRecovery::SizeOnly;
    int                      monitorIndex = -1;
    bool                     bApplied     = false;
};

[[nodiscard]] YA_GUI_API FWindowScreenPlacement queryWindowScreenPlacement(INativeWindow& window);

/// Apply size always. Origin is applied when the persisted monitor still
/// exists. Missing monitors relocate onto the primary (or name-matched)
/// display instead of leaving the window off-screen. Editor/Dock must not
/// call SDL themselves.
YA_GUI_API FWindowPlacementApplyResult recoverWindowScreenPlacement(INativeWindow&                 window,
                                                                    const FWindowScreenPlacement& placement);
YA_GUI_API bool applyWindowScreenPlacement(INativeWindow& window, const FWindowScreenPlacement& placement);

[[nodiscard]] YA_GUI_API int nativeDisplayCount();

/// Full display bounds for the monitor that currently owns `window`. Overlay
/// hosts use this rather than usable-work-area so the click-through cover
/// includes the system menu strip.
[[nodiscard]] YA_GUI_API bool queryNativeDisplayBounds(INativeWindow& window,
                                                       int&           x,
                                                       int&           y,
                                                       int&           w,
                                                       int&           h);

} // namespace ya
