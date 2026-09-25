#include "GUI/Host/GUIDockNativePlacement.h"

#include "GUI/Host/GUIAppHost.h"

#include <algorithm>
#include <cmath>

namespace ya
{

GUIWindowId realizeNativeDockPlacement(IGUIWindowCoordinator& coordinator,
                                       FDockContext&          dock,
                                       FDockFloatingWindowId  placementId,
                                       IGUIAppDelegate&       content,
                                       IRender*               render,
                                       EWindowChromeMode      chromeMode)
{
    const FDockContext::FDockFloatingPlacement* placement = dock.findFloatingById(placementId);
    if (!placement || placement->projection != EDockFloatingProjection::NativeWindow) {
        return 0;
    }
    if (placement->targetWindowId != 0) {
        if (IGUIWindowSession* existing = coordinator.findSession(placement->targetWindowId)) {
            return existing->id();
        }
    }

    FGUIWindowHostConfig config;
    config.chromeMode = chromeMode;
    config.width  = static_cast<uint32_t>(std::max(placement->size.x, 1.0f));
    config.height = static_cast<uint32_t>(std::max(placement->size.y, 1.0f));
    if (placement->geometrySpace == EDockGeometrySpace::Screen) {
        config.bHasPosition = true;
        config.posX         = static_cast<int>(std::lround(placement->pos.x));
        config.posY         = static_cast<int>(std::lround(placement->pos.y));
    }
    if (!placement->panelIds.empty()) {
        if (const FDockContext::FPanel* panel = dock.findPanel(placement->panelIds.front())) {
            config.title = panel->name.empty() ? "Dock" : panel->name;
        }
    }
    if (config.title.empty()) {
        config.title = "Dock";
    }

    const GUIWindowId id = coordinator.createSession(config, content, render);
    if (id == 0) {
        return 0;
    }
    if (!dock.bindFloatingTargetWindow(placementId, id)) {
        (void)coordinator.destroySession(id);
        return 0;
    }
    return id;
}

} // namespace ya
