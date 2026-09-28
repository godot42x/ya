#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's render settings: the clock and what resolution its viewport
/// renders at. One per App, and **settings only** -- which View this app
/// displays, and that View's camera, is an arrangement of one frame and lives
/// in `DisplayedView`.
///
/// That split is the point of the name. This struct used to also carry
/// `view`/`projection`/`cameraPos` copied out of a `SceneViewDesc`, so it held
/// both a setting and a copy of a View's declaration, and a reader could not
/// tell which of the two it was looking at.
///
/// **Default: the window and the render resolution are the same size.** A
/// resize of the main window writes `renderResolution` (`FollowWindow`), so
/// the view, the game UI, and the swapchain stay 1:1: square pixels stay
/// square, and a click lands on the widget that was drawn. `ExplicitStretch`
/// is the opt-out a caller asks for (automation, a settings UI): the window
/// keeps its size and the presentation pass stretches that image onto the
/// swapchain, so pointer hits are mapped from window space back into the
/// resolution. `Hold` leaves the seed alone — an editor panel is the
/// viewport, not the window.
///
/// The stretch, when a caller asked for it, fills the swapchain. A window
/// whose aspect differs from the resolution shows that difference.
/// Letterboxing would be a presentation feature with its own pixels to
/// choose, not a value to smuggle in here.
///
/// Every field has exactly one per-tick writer, so "who set this, and when" is
/// answered here instead of by reading the tick in order:
/// - `clock`: `GameRuntimeTickOrchestrator::prepareHostViewState`.
/// - `renderResolution`, `renderScale`, `resolutionPolicy`: `AppRenderServices`.
///   Seeded from the window at init under `FollowWindow`.
///   `App::handleWindowResized` writes the client size while that policy holds.
///   `setRenderResolution` switches to `ExplicitStretch`.
enum class EHostResolutionPolicy : uint8_t
{
    /// Resize writes the window client size. Presentation stays 1:1.
    FollowWindow = 0,
    /// A caller chose the resolution. The window stays put and the image stretches.
    ExplicitStretch,
    /// Leave the seeded resolution alone. The window is not this view.
    Hold,
};

struct HostRenderSettings
{
    HostClockState clock{};
    /// Offscreen resolution of the host viewport's View, in pixels. Under
    /// `FollowWindow` this is the window client size; under `ExplicitStretch`
    /// it is the caller's resolution and the window may differ.
    EHostResolutionPolicy resolutionPolicy = EHostResolutionPolicy::FollowWindow;
    Extent2D              renderResolution = {};
    float                 renderScale      = 1.0f;
};

} // namespace ya
