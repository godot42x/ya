#pragma once

#include "Core/Base.h"
#include "ECS/Component.h"
#include "ECS/Entity.h"

#include "Core/Math/Math.h"

#include "Core/Reflection/Reflection.h"

namespace ya
{

/// Perspective uses a vertical FOV. Orthographic uses a vertical half-extent;
/// the horizontal half-extent is `orthoHalfHeight * effectiveAspect`.
enum class ECameraProjection : uint8_t
{
    Perspective = 0,
    Orthographic,
};

struct YA_ECS_SYSTEMS_API CameraComponent : public IComponent
{
    YA_REFLECT_BEGIN(CameraComponent)
    YA_REFLECT_FIELD(bPrimary)
    YA_REFLECT_FIELD(_fixedAspectRatio)
    YA_REFLECT_FIELD(_projection)
    YA_REFLECT_FIELD(_fov)
    YA_REFLECT_FIELD(_orthoHalfHeight)
    YA_REFLECT_FIELD(_pixelPerfect, .category("Pixel Perfect").tooltip("Integer zoom: one world texel covers whole device pixels"))
    YA_REFLECT_FIELD(_pixelsPerUnit, .category("Pixel Perfect").tooltip("Texels per world unit. Tiny Town tiles are 16"))
    YA_REFLECT_FIELD(_referenceHeightPx, .category("Pixel Perfect").tooltip("Reference view height in texels. Zoom is floor(viewHeight / this), at least 1"))
    YA_REFLECT_FIELD(_aspectRatio)
    YA_REFLECT_FIELD(_nearClip)
    YA_REFLECT_FIELD(_farClip)
    YA_REFLECT_FIELD(_distance)
    YA_REFLECT_FIELD(_focusPoint)
    YA_REFLECT_METHOD(setAspectRatio)
    YA_REFLECT_END()

    bool               bPrimary          = false; // Default camera for WorldView[0]; not "the only viewport"
    bool               _fixedAspectRatio = false;
    ECameraProjection  _projection       = ECameraProjection::Perspective;

    float _fov             = 45.0f;
    /// Vertical half-extent of the orthographic volume. Ignored in perspective,
    /// and ignored while `_pixelPerfect` computes the half-extent from the view.
    float _orthoHalfHeight = 5.0f;
    /// Off by default: perspective cameras and existing scenes keep a fixed frustum.
    bool  _pixelPerfect       = false;
    /// Texels along one world unit. Used only when `_pixelPerfect` is set.
    float _pixelsPerUnit      = 16.0f;
    /// Reference view height in texels. 192 is 12 tiles of a 16-texel grid, which
    /// is the 1280x720 framing the 2D town camera was authored at (zoom 3, half height 7.5).
    float _referenceHeightPx  = 192.0f;
    /// Authored aspect. Used only when `_fixedAspectRatio` is set; otherwise the
    /// caller passes the View output aspect into `getProjection`.
    float _aspectRatio     = 16.0f / 9.0f;
    float _nearClip        = 0.1f;
    float _farClip         = 1000.0f;

    float     _distance   = 6.f;
    glm::vec3 _focusPoint = glm::vec3(0.f, 0.f, 0.f);

    /// `outputAspect` is width/height of the View this projection is for.
    /// When `_fixedAspectRatio` is set, the authored `_aspectRatio` wins.
    /// Does not read a window, a swapchain, or the owner entity.
    /// Pixel-perfect zoom needs the view's pixel height; this overload has none,
    /// so it keeps the authored ortho height. Views call `getProjection(extent)`.
    [[nodiscard]] glm::mat4 getProjection(float outputAspect) const;
    [[nodiscard]] glm::mat4 getProjection(glm::vec2 outputExtentPx) const;

    void setAspectRatio(float aspectRatio) { _aspectRatio = aspectRatio; }
};

/// Ortho size, integer zoom and the device-pixel snap for one view extent.
/// Perspective leaves the ortho fields at 0 and still fills `aspect`.
/// `zoom` is 0 when pixel-perfect is off. `snapStep` is world units per device
/// pixel (0 when off). An odd view extent sets that axis's phase to half a
/// device pixel so a texel edge lands on a pixel edge, not a pixel center.
struct YA_ECS_SYSTEMS_API CameraViewFraming
{
    float halfWidth   = 0.0f;
    float halfHeight  = 0.0f;
    float aspect      = 1.0f;
    int   zoom        = 0;
    float snapStep    = 0.0f;
    float snapPhaseX  = 0.0f;
    float snapPhaseY  = 0.0f;
};

/// The only ortho-size formula. `outputExtentPx` is the View's output in device
/// pixels; a non-positive axis means the caller has no pixel size (authored
/// height, no snap). Pixel-perfect uses the real extent aspect so texels stay
/// square — a pinned `_aspectRatio` would stretch them.
[[nodiscard]] YA_ECS_SYSTEMS_API CameraViewFraming resolveCameraViewFraming(const CameraComponent& camera,
                                                                           float                  outputAspect,
                                                                           glm::vec2              outputExtentPx);

/// View, projection and the eye the view was built from. Pixel-perfect snaps
/// the eye onto the device-pixel grid; the owner's transform is not written.
struct YA_ECS_SYSTEMS_API CameraRenderMatrices
{
    glm::mat4         view       = glm::mat4(1.0f);
    glm::mat4         projection = glm::mat4(1.0f);
    glm::vec3         cameraPos  = glm::vec3(0.0f);
    CameraViewFraming framing{};
};

[[nodiscard]] YA_ECS_SYSTEMS_API CameraRenderMatrices buildCameraRenderMatrices(const CameraComponent& camera,
                                                                               Entity*                owner,
                                                                               glm::vec2              outputExtentPx);

/// `world.viewSize()` value. Pixel-perfect orthographic cameras report
/// `(halfWidth, halfHeight)` in world units from `resolveCameraViewFraming`.
/// Every other camera reports `outputExtentPx` (the historical pixel size).
/// An unready extent (height < 1) reports `(0, 0)` while pixel-perfect is on,
/// so a script can wait instead of clamping to the authored height.
[[nodiscard]] YA_ECS_SYSTEMS_API glm::vec2 scriptViewSize(const CameraComponent* camera, glm::vec2 outputExtentPx);

/// `world.viewAspect()` value: width/height. Pixel-perfect ortho uses
/// `halfWidth / halfHeight` from the same framing, which is the output aspect.
[[nodiscard]] YA_ECS_SYSTEMS_API float scriptViewAspect(const CameraComponent* camera, glm::vec2 outputExtentPx);

/// Snap X/Y onto `step`, shifted by `phase` (0, or half a device pixel on an
/// odd axis). Z is left alone. A non-positive step returns `position`.
[[nodiscard]] YA_ECS_SYSTEMS_API glm::vec3 snapWorldXY(glm::vec3 position, float step, float phaseX, float phaseY);

/// View matrix from the owner's world pose. Identity when the owner has no
/// usable transform. Does not write the transform. Unsnapped: the rendered
/// view goes through `buildCameraRenderMatrices`.
[[nodiscard]] YA_ECS_SYSTEMS_API glm::mat4 cameraViewFromOwner(Entity* owner);

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ECameraProjection)
YA_REFLECT_ENUM_VALUE(Perspective)
YA_REFLECT_ENUM_VALUE(Orthographic)
YA_REFLECT_ENUM_END()
