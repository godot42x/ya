#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's own view state: the clock, the resolution its viewport renders at,
/// and the camera of the View that owns that viewport. One per App - not a
/// per-View packet and not present/swapchain state.
///
/// **The window is the presentation surface, not the render size.** The render
/// resolution is a setting: the host viewport's View is sized from it and the
/// presentation pass stretches that image onto the swapchain image, which may be
/// a different size. Resizing the window therefore never changes what is
/// rendered, only how it is shown, and lowering the resolution is a supported way
/// to spend less time rendering. Nothing here is derived from the window.
///
/// (The aspect consequence is deliberate and pre-existing: the presentation
/// stretch is 1:1 in pixels of the *render* image, so a window whose aspect
/// differs from the resolution's shows that difference. Letterboxing or fitting
/// would be a presentation feature with its own pixels to choose, not a value to
/// smuggle in here.)
///
/// Every field has exactly one per-tick writer, so "who set this, and when" is
/// answered here instead of by reading the tick in order:
/// - `clock`: `GameRuntimeTickOrchestrator::prepareHostViewState`.
/// - `renderResolution` and `viewportFrameBufferScale`: the render settings,
///   through `AppRenderServices`. Seeded once from the size the window was
///   created with, then only changed by a caller asking for a different
///   resolution (the control plane, a settings UI); a resize of the window is
///   not such a caller.
/// - `view`, `projection`, `cameraPos`: `GameRuntimeTickOrchestrator::declareViews`.
///   It adopts whatever View owns the host viewport this tick, or drops the
///   camera to identity when no View declared one. A View's own rect is not
///   copied here: it belongs to the declaration, and a reader that wants the
///   rectangle the host viewport was rendered at reads the renderer's published
///   output (`RenderDeviceState::getViewportExtent`).
struct HostViewState
{
    HostClockState clock{};
    /// Offscreen resolution of the host viewport's View, in pixels. The setting
    /// above; never the window's client size.
    Extent2D  renderResolution         = {};
    float     viewportFrameBufferScale = 1.0f;
    glm::mat4 view                     = glm::mat4(1.0f);
    glm::mat4 projection               = glm::mat4(1.0f);
    glm::vec3 cameraPos                = glm::vec3(0.0f);
};

} // namespace ya
