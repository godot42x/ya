#pragma once

#include "Core/Base.h"
#include "Render3D/Common/SceneViewDesc.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace ya
{

/// The Scene View this app displays this frame, as the recording left it.
///
/// Named for what it answers -- "which View is being shown, in which flight,
/// from which camera" -- and for nothing it does not claim: not "the host", not
/// "the primary", not "fullscreen". A game window displays it as the whole
/// image; the editor displays it inside its viewport panel while the chrome
/// fills the window. Either way it is the View whose output feeds this frame's
/// display.
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
/// Split-screen shape: extra Views compose onto this one as `ViewDisplayInset`s
/// (declared on their own `SceneViewDesc::composeRect`), so this stays the one
/// arrangement of the display root -- a second inset is not a second surface.
/// A future presentation layout generalizes "one display root + N insets"; this
/// struct is its N=1 spelling.
///
/// `viewId == 0` means this frame showed no View; `flightIndex ==
/// MAX_FLIGHTS_IN_FLIGHT` means no frame has been recorded. Both are answers.
struct DisplayedView
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
