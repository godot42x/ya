#pragma once

#include "Core/Base.h"

#include "Render3D/Common/HostClockState.h"

#include <glm/glm.hpp>

namespace ya
{

/// The host's own view state for the current tick: clock and viewport geometry,
/// plus the camera of the primary view. The host owns the geometry; the camera
/// is the one the producer that declared the primary view supplied, kept here so
/// the camera packet, offscreen resizes and the automation surface read one copy.
/// One per App - not a per-View packet and not present/swapchain state.
///
/// Every field has exactly one per-tick writer, so "who set this, and when" is
/// answered here instead of by reading the tick in order:
/// - `clock`: `GameRuntimeTickOrchestrator::prepareHostViewState`.
/// - `viewportFrameBufferScale`: `AppRenderServices::setViewportFrameBufferScale`
///   (the render settings that own it), default 1.
/// - `viewportRect`, `view`, `projection`, `cameraPos`:
///   `GameRuntimeTickOrchestrator::declareViews`. It adopts whatever View owns
///   the host viewport this tick, or drops the camera to identity and leaves the
///   geometry at the host's request when no View declared one.
///   `AppRenderServices::setViewportRect` is the only other writer of
///   `viewportRect` and it is the host's *request* (seeded once from the window
///   the surface was created with, then overridden by the control plane), not a
///   per-tick write: a decision about where the host viewport is belongs to the
///   View that declares it, so the next tick's declaration replaces the request.
struct HostViewState
{
    HostClockState clock{};
    Rect2D    viewportRect             = {};
    float     viewportFrameBufferScale = 1.0f;
    glm::mat4 view                     = glm::mat4(1.0f);
    glm::mat4 projection               = glm::mat4(1.0f);
    glm::vec3 cameraPos                = glm::vec3(0.0f);
};

} // namespace ya
