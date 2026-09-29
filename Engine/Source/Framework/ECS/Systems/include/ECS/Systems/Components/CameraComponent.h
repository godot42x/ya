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
    YA_REFLECT_FIELD(bPrimary, .script("primary"))
    YA_REFLECT_FIELD(_fixedAspectRatio, .script())
    YA_REFLECT_FIELD(_projection, .script())
    YA_REFLECT_FIELD(_fov, .script())
    YA_REFLECT_FIELD(_orthoHalfHeight, .script())
    YA_REFLECT_FIELD(_aspectRatio, .script())
    YA_REFLECT_FIELD(_nearClip, .script())
    YA_REFLECT_FIELD(_farClip, .script())
    YA_REFLECT_FIELD(_distance, .script())
    YA_REFLECT_FIELD(_focusPoint, .script())
    YA_REFLECT_METHOD(setAspectRatio, .script())
    YA_REFLECT_END()

    bool               bPrimary          = false; // Default camera for WorldView[0]; not "the only viewport"
    bool               _fixedAspectRatio = false;
    ECameraProjection  _projection       = ECameraProjection::Perspective;

    float _fov             = 45.0f;
    /// Vertical half-extent of the orthographic volume. Ignored in perspective.
    float _orthoHalfHeight = 5.0f;
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
    [[nodiscard]] glm::mat4 getProjection(float outputAspect) const;

    void setAspectRatio(float aspectRatio) { _aspectRatio = aspectRatio; }
};

/// View matrix from the owner's world pose. Identity when the owner has no
/// usable transform. Does not write the transform.
[[nodiscard]] YA_ECS_SYSTEMS_API glm::mat4 cameraViewFromOwner(Entity* owner);

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ECameraProjection)
YA_REFLECT_ENUM_VALUE(Perspective)
YA_REFLECT_ENUM_VALUE(Orthographic)
YA_REFLECT_ENUM_END()
