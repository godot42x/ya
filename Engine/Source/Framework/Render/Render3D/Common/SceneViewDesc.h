#pragma once

#include "Core/Common/Types.h"
#include "Render3D/Common/RenderFeatures.h"

#include <cstdint>
#include <glm/glm.hpp>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;

using SceneViewId = uint64_t;

/// Stable persistent-key slot for the host WorldView. Overlay Views use other
/// host-assigned ids; they must not resize this View's output identity.
inline constexpr SceneViewId kPrimarySceneViewId = 1;

[[nodiscard]] inline glm::mat4 makeCameraViewProjection(const glm::mat4& projection, const glm::mat4& view)
{
    return projection * view;
}

/// One View an owner declares for this tick: which Scene content to draw, from
/// which camera, into which output.
///
/// This is the single declaration structure. The scheduler consumes it
/// directly instead of translating a second struct with the same fields, and
/// the sealed plan keeps it verbatim on the View's plan entry, so a declared
/// View has exactly one spelling between its owner and its recording.
///
/// Pure declaration: no extraction callback and no GPU handles. `scene` is
/// tick-local and owned by the declarer; `sceneRevision` is part of the content
/// key, not Scene state.
struct SceneViewDesc
{
    Scene*      scene         = nullptr;
    SceneViewId viewId        = 0;
    uint64_t    sceneRevision = 0;
    /// GPU-family policy: two Views of one Scene share a family (and therefore
    /// that Scene's GPU packets) only when they also share this. Not a View
    /// identity and not a Scene property.
    uint64_t    policyId      = 1;

    glm::mat4 view       = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::vec3 cameraPos  = glm::vec3(0.0f);
    /// This View's own offscreen camera rect (origin at its RT top-left). Not
    /// chrome widget offset, and not the compose dest on another View.
    Rect2D    viewportRect{};
    /// 0: this View is a display root (the host viewport identity). Non-zero:
    /// blit `composeRect` onto that View's display RT after recording.
    SceneViewId composeOntoViewId = 0;
    Rect2D      composeRect{};

    /// What this View draws (see RenderFeatures.h). The declarer decides: the
    /// editor's views include generated editor companions, a game view does not.
    FRenderFeatureMask features = toMask(ERenderFeature::Game);
    /// The entity this View is rendered from, when one owns it. That entity's
    /// generated companions are dropped from this View, so a camera preview does
    /// not draw the camera's own body. entt::null when no entity owns the View
    /// (the editor's authoring camera is not a Scene entity).
    entt::entity viewOwner = entt::null;

    [[nodiscard]] bool ownsHostViewport() const { return composeOntoViewId == 0; }

    /// Derived on read: `view` and `projection` are the truth, and a stored
    /// product would be one more copy that can disagree with them.
    [[nodiscard]] glm::mat4 viewProjection() const { return makeCameraViewProjection(projection, view); }
};

} // namespace ya
