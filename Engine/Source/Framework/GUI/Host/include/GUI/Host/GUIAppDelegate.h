#pragma once

#include "Core/Event.h"
#include "GUI/Widgets/WidgetTree.h"

namespace ya
{

/// Contract implemented by a GUI app's presentation state. Windowed,
/// offscreen, and headless hosts all own the tree/frame lifecycle; delegates
/// only mount widgets and synchronize state before snapshot generation.
struct IGUIAppDelegate
{
    virtual ~IGUIAppDelegate() = default;

    /// Mount the app's widgets once after the host creates its WidgetTree.
    virtual void buildUI(WidgetTree& tree) = 0;

    /// Synchronize widget state immediately before buildSnapshot(). Widget
    /// mutation never occurs during command recording.
    virtual void updateUI() {}

    /// Optional observation of a routed UI event (smoke logs / diagnostics).
    virtual void onRoutedEvent(const Event& /*event*/, EWidgetRouteResult /*result*/) {}

    /// App-driven graceful shutdown request observed at a frame boundary.
    [[nodiscard]] virtual bool shouldRequestClose() const { return false; }
};

} // namespace ya
