#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneViewDesc.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace ya
{

/// The View the host window shows, as the frame that was just recorded left it.
///
/// This is an **arrangement**, not renderer state and not a setting: the app
/// derives it from the plan's display root (the View whose `composeOntoViewId == 0`),
/// and the renderer only ever stores every View's output. It answers two
/// questions a reader cannot answer from the renderer: which View is shown, and
/// in which flight its output was published.
///
/// The camera is here because it is needed *after* the plan is consumed -- the
/// editor's viewport overlay and picking run in the same tick (overlay) or
/// before the next one (picking), and the plan's `SceneViewDesc` is owned by the
/// recording. It is a copy of that declaration's camera for the frame, named as
/// the arrangement it belongs to, rather than a second "host state".
///
/// `viewId == 0` means this frame showed no View; `flightIndex ==
/// MAX_FLIGHTS_IN_FLIGHT` means no frame has been recorded. Both are answers.
struct HostViewportView
{
    SceneViewId viewId      = 0;
    uint32_t    flightIndex = MAX_FLIGHTS_IN_FLIGHT;

    glm::mat4   view{1.0f};
    glm::mat4   projection{1.0f};
    glm::vec3   cameraPos{0.0f};

    [[nodiscard]] bool isBound() const { return viewId != 0; }
    [[nodiscard]] glm::mat4 viewProjection() const { return projection * view; }
};

} // namespace ya
