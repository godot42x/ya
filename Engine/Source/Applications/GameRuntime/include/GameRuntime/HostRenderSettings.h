#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's render settings: the clock and what resolution its viewport
/// renders at. One per App, and **settings only** -- which View the host window
/// shows, and that View's camera, is an arrangement of one frame and lives in
/// `HostViewportView`.
///
/// That split is the point of the name. This struct used to also carry
/// `view`/`projection`/`cameraPos` copied out of a `SceneViewDesc`, so it held
/// both a setting and a copy of a View's declaration, and a reader could not
/// tell which of the two it was looking at.
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
/// - `renderResolution` and `renderScale`: the render settings,
///   through `AppRenderServices`. Seeded once from the size the window was
///   created with, then only changed by a caller asking for a different
///   resolution (the control plane, a settings UI); a resize of the window is
///   not such a caller.
struct HostRenderSettings
{
    HostClockState clock{};
    /// Offscreen resolution of the host viewport's View, in pixels. The setting
    /// above; never the window's client size.
    Extent2D  renderResolution         = {};
    float     renderScale = 1.0f;
};

} // namespace ya
