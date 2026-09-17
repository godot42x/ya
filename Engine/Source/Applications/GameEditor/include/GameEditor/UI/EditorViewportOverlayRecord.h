#pragma once

#include "Core/Common/Types.h"

#include <glm/glm.hpp>

namespace ya
{

struct EditorLayer;

/// Record Render2D commands for the 3D/world viewport overlay pass.
/// Call only inside `recordRender2DComposePass` (EditorViewportCompose).
/// `bDepthTestedWorld` enables collision wireframes and selection AABBs;
/// those need the scene depth attachment on the compose pass.
void recordEditorWorldViewportOverlays(EditorLayer& layer, bool bDepthTestedWorld);

/// Selection outline + resize handles for the 2D canvas preview, in
/// render-target pixels. Uses the same uiScale/offset as the preview snapshot.
void recordEditorCanvasSelectionOverlay(const Rect2D& rect,
                                        const glm::vec2& uiScale,
                                        const glm::vec2& offset);

} // namespace ya
