#pragma once

#include "Core/Base.h"
#include "Core/Common/Types.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>
#include <optional>

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
/// **Default: the render resolution is the window's drawable size.** A
/// resize of the main window writes that pixel size (`FollowWindow`), so
/// the view and the swapchain stay 1:1. `pixelDensity` is drawable pixels
/// per logical point, and game UI lays out in logical points
/// (`renderResolution / pixelDensity`). `ExplicitStretch`
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
/// - `renderResolution`, `pixelDensity`, `logicalViewport`, `renderScale`,
///   `resolutionPolicy`: `AppRenderServices`.
///   Seeded from the window drawable at init under `FollowWindow`.
///   `App::handleWindowResized` writes the drawable size while that policy holds.
///   `setRenderResolution` switches to `ExplicitStretch`.
enum class EHostResolutionPolicy : uint8_t
{
    /// Resize writes the window drawable size. Presentation stays 1:1.
    FollowWindow = 0,
    /// A caller chose the resolution. The window stays put and the image stretches.
    ExplicitStretch,
    /// Leave the seeded resolution alone. The window is not this view.
    Hold,
};

struct HostRenderSettings
{
    HostClockState clock{};
    /// Offscreen resolution of the host viewport's View, in device pixels. Under
    /// `FollowWindow` this is the window drawable; under `ExplicitStretch`
    /// it is the caller's resolution and the window may differ. Under `Hold`
    /// the editor writes the panel's device-pixel size.
    EHostResolutionPolicy resolutionPolicy = EHostResolutionPolicy::FollowWindow;
    Extent2D              renderResolution = {};
    /// Device pixels per logical point of `logicalViewport`. Not `renderScale`.
    float                 pixelDensity     = 1.0f;
    /// The host viewport in window logical points. The full window under
    /// `FollowWindow`; the editor panel under `Hold`. Empty until the first
    /// seed. Pointer hits subtract `pos` and multiply by `pixelDensity` to
    /// land in the view's device pixels.
    Rect2D                logicalViewport{};
    /// Host supersample setting. It does not size the view and it is not the
    /// logical-to-device density.
    float                 renderScale      = 1.0f;
};

/// Window logical point → view device pixels. Empty when the point is outside
/// `logicalRect`. A non-positive extent treats the window origin as the view
/// origin and still applies `density`.
[[nodiscard]] inline std::optional<glm::vec2> mapWindowPointToViewPixels(glm::vec2      windowPoint,
                                                                        const Rect2D& logicalRect,
                                                                        float         density)
{
    const float scale = density > 0.0f ? density : 1.0f;
    if (logicalRect.extent.x <= 0.0f || logicalRect.extent.y <= 0.0f) {
        return windowPoint * scale;
    }
    const glm::vec2 local = windowPoint - logicalRect.pos;
    if (local.x < 0.0f || local.y < 0.0f || local.x > logicalRect.extent.x || local.y > logicalRect.extent.y) {
        return std::nullopt;
    }
    return local * scale;
}

} // namespace ya
