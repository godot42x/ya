#pragma once

#include "Core/Common/Types.h"
#include "Render3D/Common/RenderFeatures.h"

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

#include "entt/entt.hpp"

namespace ya
{

struct Scene;

using SceneViewId = uint64_t;

/// Owner half of a View's identity. An opaque id a producer names for itself;
/// the framework does not enumerate the products that own Views.
using SceneViewOwnerId = uint32_t;

/// A View's identity, scoped to whoever owns it.
///
/// This used to be a global small integer: `kPrimarySceneViewId = 1` was the
/// persistent identity of *both* products' host viewport -- the standalone game
/// view and the editor's authoring view -- and the camera preview was minted at
/// 2 inside the editor. Two owners sharing one id space means neither can have
/// its own "primary", and "which View is this" could only be answered by going
/// and reading the declarer.
///
/// A key is (owner, local): the producer names its owner and mints local ids
/// inside it. The flat id the output tables key on is derived from the key --
/// those tables never needed a compound key, the *source* of identity did.
struct SceneViewKey
{
    SceneViewOwnerId owner = 0;
    uint32_t         local = 0;

    /// A key is an identity only when both halves are set. An unnamed owner or
    /// a 0 local is not "the View at 0"; it is absent, and `viewId()` reports it
    /// as the same 0 the output tables already read as "no View".
    [[nodiscard]] constexpr bool valid() const { return owner != 0 && local != 0; }

    [[nodiscard]] constexpr SceneViewId viewId() const
    {
        return valid() ? (static_cast<SceneViewId>(owner) << 32) | static_cast<SceneViewId>(local) : 0;
    }

    friend constexpr bool operator==(const SceneViewKey&, const SceneViewKey&) = default;
};

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
    /// This View's render target, in device pixels (origin at the RT top-left).
    /// Not the chrome widget's logical rect and not the window.
    Rect2D    outputRect{};
    /// Device pixels per logical point of the surface that shows this View.
    /// 1 when the declarer has no separate logical space. Game UI divides
    /// `outputRect` by this to get the logical canvas. This is not
    /// `renderScale` (the host supersample setting).
    float pixelDensity = 1.0f;
    /// Whether this declaration's output is what a display shows: the surface's
    /// fullscreen image in a game, the panel's image in the editor. False marks
    /// a View rendered as material for something else to sample (the editor's
    /// camera preview); it must not claim the display. There is no "compose
    /// onto another View" here -- where an output is shown is the display
    /// layer's decision (GUI layout, surface backdrop), never the View's.
    bool bDisplayRoot = true;

    /// What this View draws (see RenderFeatures.h). The declarer decides: the
    /// editor's views include generated editor companions, a game view does not.
    FRenderFeatureMask features = toMask(ERenderFeature::Game);
    /// The entity this View is rendered from, when one owns it. That entity's
    /// generated companions are dropped from this View, so a camera preview does
    /// not draw the camera's own body. entt::null when no entity owns the View
    /// (the editor's authoring camera is not a Scene entity).
    entt::entity viewOwner = entt::null;

    /// Derived on read: `view` and `projection` are the truth, and a stored
    /// product would be one more copy that can disagree with them.
    [[nodiscard]] glm::mat4 viewProjection() const { return makeCameraViewProjection(projection, view); }
};

/// Whole device pixels for a logical extent. `pixelDensity` is device pixels
/// per logical point; a non-positive density is 1.
[[nodiscard]] inline glm::vec2 devicePixelExtent(glm::vec2 logicalExtent, float pixelDensity)
{
    const float density = pixelDensity > 0.0f ? pixelDensity : 1.0f;
    return {
        std::round(logicalExtent.x * density),
        std::round(logicalExtent.y * density),
    };
}

} // namespace ya
