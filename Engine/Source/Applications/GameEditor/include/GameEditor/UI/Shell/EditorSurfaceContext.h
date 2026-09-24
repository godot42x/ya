#pragma once

#include "Core/Common/Types.h"

#include <glm/glm.hpp>

namespace ya
{

struct App;
struct DisplayedView;
struct IRenderSurfaceContext;
struct WidgetTree;

/// Per-window chrome metrics. Not a Camera extent and not a present flight.
/// Hosts fill this from the native window + its present surface; EditorSurface
/// only consumes it.
struct EditorWindowMetrics
{
    Extent2D logicalExtent{1, 1};
    Extent2D framebufferExtent{};
    float    dpiScale          = 1.0f;
    float    chromeInsetLeft   = 0.0f;
    float    chromeInsetTop    = 0.0f;
    float    chromeInsetRight  = 0.0f;
    float    chromeInsetBottom = 0.0f;
    /// Trailing Hybrid/ClientDrawn title drag gutter used to inset page tabs.
    /// Empty title space is Drag; tab buttons are registered as Client hits.
    float    chromeDragGutter  = 0.0f;
    int      screenX           = 0;
    int      screenY           = 0;
    int      monitorIndex      = -1;
    bool     bMaximized        = false;
    bool     bHasScreenOrigin  = false;
};

/// Layer, tree, spawners and viewport host stay Surface-owned. Selection /
/// actions / undo live on the window's active EditorRootSession (ES-3).
/// View/projection come from the host viewport's View (the app's arrangement),
/// not from swapchain state.
struct FEditorSurfaceContext
{
    App*                     app = nullptr;
    IRenderSurfaceContext*   presentSurface = nullptr;
    EditorWindowMetrics      metrics;
    glm::mat4                view{1.0f};
    glm::mat4                projection{1.0f};
};

/// Derive tree extent and dpi from raw window/framebuffer numbers.
/// When both logical and framebuffer widths are positive, dpi is
/// framebuffer/logical (same ratio the old applyWindowMetrics used).
[[nodiscard]] EditorWindowMetrics makeEditorWindowMetrics(int windowW,
                                                          int windowH,
                                                          float windowDpi,
                                                          Extent2D framebufferExtent);

void applyEditorWindowMetrics(WidgetTree& tree, const EditorWindowMetrics& metrics);

[[nodiscard]] FEditorSurfaceContext makeEditorSurfaceContext(
    App& app,
    IRenderSurfaceContext& surface,
    const DisplayedView& displayedView);

} // namespace ya
