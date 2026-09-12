#include "GUI/Host/GUIWindowPlacement.h"

#include "Core/Os/Os.h"
#include "RHI/NativeWindow.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace ya
{

int nativeDisplayCount()
{
    return Os::displayCount();
}

namespace
{

[[nodiscard]] int indexOfDisplayName(std::string_view name)
{
    if (name.empty()) {
        return -1;
    }
    const int count = Os::displayCount();
    for (int i = 0; i < count; ++i) {
        if (Os::displayName(i) == name) {
            return i;
        }
    }
    return -1;
}

} // namespace

FWindowScreenPlacement queryWindowScreenPlacement(INativeWindow& window)
{
    FWindowScreenPlacement placement;
    window.getWindowSize(placement.w, placement.h);
    placement.bHasOrigin   = window.getWindowPosition(placement.x, placement.y);
    placement.monitorIndex = window.getDisplayIndex();
    placement.monitorName  = window.getDisplayName();
    placement.bMaximized   = window.isMaximized();
    placement.bMinimized   = window.isMinimized();
    return placement;
}

FWindowPlacementApplyResult recoverWindowScreenPlacement(INativeWindow&                 window,
                                                         const FWindowScreenPlacement& placement)
{
    FWindowPlacementApplyResult result;
    const int width  = std::max(placement.w, 1);
    const int height = std::max(placement.h, 1);
    result.bApplied = window.setWindowSize(width, height);

    const int count = nativeDisplayCount();
    const bool bIndexValid = placement.monitorIndex >= 0 && placement.monitorIndex < count;
    const bool bClaimedMonitor =
        placement.monitorIndex >= 0 || !placement.monitorName.empty();
    int targetMonitor = -1;
    if (bIndexValid) {
        targetMonitor = placement.monitorIndex;
    }
    else {
        targetMonitor = indexOfDisplayName(placement.monitorName);
        if (targetMonitor < 0 && bClaimedMonitor && count > 0) {
            targetMonitor = 0;
        }
    }
    result.monitorIndex = targetMonitor;

    if (placement.bHasOrigin && bIndexValid) {
        result.bApplied = window.setWindowPosition(placement.x, placement.y) && result.bApplied;
        result.recovery = EWindowPlacementRecovery::Applied;
    }
    else if (placement.bHasOrigin && bClaimedMonitor && targetMonitor >= 0) {
        int displayX = 0;
        int displayY = 0;
        int displayW = 0;
        int displayH = 0;
        if (Os::displayBounds(targetMonitor, displayX, displayY, displayW, displayH, true)) {
            (void)displayW;
            (void)displayH;
            const int x = displayX + 48;
            const int y = displayY + 48;
            result.bApplied = window.setWindowPosition(x, y) && result.bApplied;
            result.recovery = EWindowPlacementRecovery::Relocated;
        }
        else {
            result.recovery = EWindowPlacementRecovery::SizeOnly;
        }
    }
    else {
        result.recovery = EWindowPlacementRecovery::SizeOnly;
    }

    if (placement.bMaximized) {
        result.bApplied = window.maximize() && result.bApplied;
    }
    else if (window.isMaximized()) {
        result.bApplied = window.restoreFromMaximize() && result.bApplied;
    }
    return result;
}

bool applyWindowScreenPlacement(INativeWindow& window, const FWindowScreenPlacement& placement)
{
    return recoverWindowScreenPlacement(window, placement).bApplied;
}

bool queryNativeDisplayBounds(INativeWindow& window, int& x, int& y, int& w, int& h)
{
    return Os::displayBounds(window.getDisplayIndex(), x, y, w, h, false);
}

} // namespace ya
