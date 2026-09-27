#pragma once

#include "Render2D/ScreenDrawList.h"
#include "Render3D/WorldDraw.h"
#include "Core/Common/Types.h"

#include <glm/glm.hpp>

namespace ya
{

struct EditorLayer;

/// Screen-space viewport chrome: gizmo and camera HUD. Drawn without a camera.
void recordEditorViewportScreenOverlays(ScreenDrawList& list, EditorLayer& layer);

/// World-space debug lines for the viewport overlay. `bDepthTestedWorld`
/// enables collision wireframes and selection AABBs; those share the scene
/// depth attachment on the compose pass. Frustum lines are always recorded.
void recordEditorViewportWorldOverlays(WorldDrawList& list, EditorLayer& layer, bool bDepthTestedWorld);

/// Selection outline + resize handles for the 2D canvas preview, in
/// render-target pixels. Uses the same uiScale/offset as the preview snapshot.
void recordEditorCanvasSelectionOverlay(ScreenDrawList& list, const Rect2D& rect,
                                        const glm::vec2& uiScale, const glm::vec2& offset);

} // namespace ya
