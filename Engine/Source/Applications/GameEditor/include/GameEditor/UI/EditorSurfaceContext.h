#pragma once

#include "Core/Common/Types.h"

#include <glm/glm.hpp>

namespace ya
{

struct App;
struct WidgetTree;

/// Per-window chrome metrics. Not a Camera extent and not a present flight.
/// Hosts fill this from the native window + its present surface; EditorSurface
/// only consumes it.
struct EditorWindowMetrics
{
    Extent2D logicalExtent{1, 1};
    Extent2D framebufferExtent{};
    float    dpiScale = 1.0f;
};

/// Per-frame inputs EditorSurface needs from the window host. Layer, tree,
/// spawners and viewport host stay Surface-owned; ES-2 session owns the Surface
/// for this window, ES-3 moves selection/undo/actions off Surface.
/// Viewport view/projection are Camera frame data, not swapchain state.
struct FEditorSurfaceContext
{
    EditorWindowMetrics metrics;
    glm::mat4           view{1.0f};
    glm::mat4           projection{1.0f};
};

/// Derive tree extent and dpi from raw window/framebuffer numbers.
/// When both logical and framebuffer widths are positive, dpi is
/// framebuffer/logical (same ratio the old applyWindowMetrics used).
[[nodiscard]] EditorWindowMetrics makeEditorWindowMetrics(int windowW,
                                                          int windowH,
                                                          float windowDpi,
                                                          Extent2D framebufferExtent);

void applyEditorWindowMetrics(WidgetTree& tree, const EditorWindowMetrics& metrics);

/// Transitional adapter: read the primary present surface + render frame
/// matrices from App. EditorSurface::tick(App&) is the only remaining caller;
/// ES-5 deletes that forwarding.
[[nodiscard]] FEditorSurfaceContext makeEditorSurfaceContext(App& app);

} // namespace ya
