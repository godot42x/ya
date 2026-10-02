#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include "Scene3D/TransformComponent.h"

#include <cmath>
#include <limits>


namespace ya
{

namespace
{

/// Where the camera actually is in the world.
///
/// A camera renders from its position in the scene, so a view built from the
/// authored local transform alone is only correct while the camera happens to
/// be a root node. The mesh drawn for the camera, the FOV wireframe and the
/// picking all resolve through the world matrix, so the view has to resolve it
/// the same way or the camera is drawn in one place and renders from another.
struct FOwnerWorldPose
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

bool resolveOwnerWorldPose(Entity* owner, FOwnerWorldPose& out)
{
    if (!owner || !owner->hasComponent<TransformComponent>()) {
        return false;
    }

    auto* tc = owner->getComponent<TransformComponent>();
    if (!tc) {
        return false;
    }

    // No-op when the transform is already clean, which is the normal case: the
    // TransformSystem runs before anything reads a view this tick.
    TransformSystem::computeWorldMatrix(tc);

    const glm::mat4& world = tc->getWorldMatrix();
    out.position           = glm::vec3(world[3]);

    // Scale is stripped before the rotation is read back: a scaled ancestor
    // belongs to the mesh, not to the camera basis.
    const glm::vec3 basisX = glm::vec3(world[0]);
    const glm::vec3 basisY = glm::vec3(world[1]);
    const glm::vec3 basisZ = glm::vec3(world[2]);
    if (glm::length2(basisX) < std::numeric_limits<float>::epsilon() ||
        glm::length2(basisY) < std::numeric_limits<float>::epsilon() ||
        glm::length2(basisZ) < std::numeric_limits<float>::epsilon()) {
        return false;
    }

    out.rotation = glm::normalize(
        glm::quat_cast(glm::mat3(glm::normalize(basisX), glm::normalize(basisY), glm::normalize(basisZ))));
    return true;
}

float snapAxis(float value, float step, float phase)
{
    if (!(step > 0.0f)) {
        return value;
    }
    const float shifted = (value - phase) / step;
    return phase + std::floor(shifted + 0.5f) * step;
}

glm::mat4 viewFromPose(const FOwnerWorldPose& pose)
{
    const glm::vec3 forward = pose.rotation * FMath::Vector::WorldForward;
    const glm::vec3 up      = pose.rotation * FMath::Vector::WorldUp;
    return FMath::lookAt(pose.position, pose.position + forward, up);
}

glm::mat4 projectionFromFraming(const CameraComponent& camera, const CameraViewFraming& framing)
{
    if (camera._projection == ECameraProjection::Orthographic) {
        return FMath::orthographic(-framing.halfWidth,
                                   framing.halfWidth,
                                   -framing.halfHeight,
                                   framing.halfHeight,
                                   camera._nearClip,
                                   camera._farClip);
    }
    return FMath::perspective(glm::radians(camera._fov), framing.aspect, camera._nearClip, camera._farClip);
}

float phaseForAxis(float step, float extentPx)
{
    if (!(step > 0.0f) || !(extentPx >= 1.0f)) {
        return 0.0f;
    }
    const int pixels = static_cast<int>(std::floor(extentPx));
    return (pixels & 1) != 0 ? 0.5f * step : 0.0f;
}

} // namespace

glm::vec3 snapWorldXY(glm::vec3 position, float step, float phaseX, float phaseY)
{
    position.x = snapAxis(position.x, step, phaseX);
    position.y = snapAxis(position.y, step, phaseY);
    return position;
}

CameraViewFraming resolveCameraViewFraming(const CameraComponent& camera,
                                          float                  outputAspect,
                                          glm::vec2              outputExtentPx)
{
    const bool bHavePixels = outputExtentPx.x > 0.0f && outputExtentPx.y >= 1.0f;
    const bool bPixelPerfectOrtho =
        camera._pixelPerfect && camera._projection == ECameraProjection::Orthographic && bHavePixels;

    CameraViewFraming framing;
    // A pinned aspect stretches a pixel-perfect view, so texels stop being square.
    // The real output aspect is what keeps one texel on an integer pixel square.
    if (bPixelPerfectOrtho) {
        framing.aspect = outputExtentPx.x / outputExtentPx.y;
    }
    else if (camera._fixedAspectRatio) {
        framing.aspect = camera._aspectRatio;
    }
    else {
        framing.aspect = outputAspect > 0.0f ? outputAspect : camera._aspectRatio;
    }

    if (camera._projection != ECameraProjection::Orthographic) {
        return framing;
    }

    const bool bCanZoom = bPixelPerfectOrtho && camera._pixelsPerUnit > 0.0f && camera._referenceHeightPx > 0.0f;
    if (!bCanZoom) {
        framing.halfHeight = camera._orthoHalfHeight;
        framing.halfWidth  = framing.halfHeight * framing.aspect;
        return framing;
    }

    const float rawZoom = std::floor(outputExtentPx.y / camera._referenceHeightPx);
    framing.zoom        = rawZoom < 1.0f ? 1 : static_cast<int>(rawZoom);
    const float zoom    = static_cast<float>(framing.zoom);
    framing.halfHeight  = outputExtentPx.y / (2.0f * camera._pixelsPerUnit * zoom);
    framing.halfWidth   = framing.halfHeight * framing.aspect;
    framing.snapStep    = 1.0f / (camera._pixelsPerUnit * zoom);
    framing.snapPhaseX  = phaseForAxis(framing.snapStep, outputExtentPx.x);
    framing.snapPhaseY  = phaseForAxis(framing.snapStep, outputExtentPx.y);
    return framing;
}

glm::mat4 CameraComponent::getProjection(float outputAspect) const
{
    return projectionFromFraming(*this, resolveCameraViewFraming(*this, outputAspect, glm::vec2(0.0f)));
}

glm::mat4 CameraComponent::getProjection(glm::vec2 outputExtentPx) const
{
    const float outputAspect = (outputExtentPx.x > 0.0f && outputExtentPx.y > 0.0f)
                                   ? outputExtentPx.x / outputExtentPx.y
                                   : _aspectRatio;
    return projectionFromFraming(*this, resolveCameraViewFraming(*this, outputAspect, outputExtentPx));
}

CameraRenderMatrices buildCameraRenderMatrices(const CameraComponent& camera, Entity* owner, glm::vec2 outputExtentPx)
{
    const float outputAspect = (outputExtentPx.x > 0.0f && outputExtentPx.y > 0.0f)
                                   ? outputExtentPx.x / outputExtentPx.y
                                   : camera._aspectRatio;
    CameraRenderMatrices matrices;
    matrices.framing    = resolveCameraViewFraming(camera, outputAspect, outputExtentPx);
    matrices.projection = projectionFromFraming(camera, matrices.framing);
    if (FOwnerWorldPose pose; resolveOwnerWorldPose(owner, pose)) {
        if (matrices.framing.snapStep > 0.0f) {
            pose.position = snapWorldXY(pose.position,
                                        matrices.framing.snapStep,
                                        matrices.framing.snapPhaseX,
                                        matrices.framing.snapPhaseY);
        }
        matrices.cameraPos = pose.position;
        matrices.view      = viewFromPose(pose);
    }
    return matrices;
}

glm::vec2 scriptViewSize(const CameraComponent* camera, glm::vec2 outputExtentPx)
{
    if (!camera || !camera->_pixelPerfect || camera->_projection != ECameraProjection::Orthographic) {
        return outputExtentPx;
    }
    if (outputExtentPx.x <= 0.0f || outputExtentPx.y < 1.0f) {
        return glm::vec2(0.0f);
    }
    const CameraViewFraming framing =
        resolveCameraViewFraming(*camera, outputExtentPx.x / outputExtentPx.y, outputExtentPx);
    return glm::vec2(framing.halfWidth, framing.halfHeight);
}

float scriptViewAspect(const CameraComponent* camera, glm::vec2 outputExtentPx)
{
    if (outputExtentPx.x <= 0.0f || outputExtentPx.y <= 0.0f) {
        return 1.0f;
    }
    if (camera && camera->_pixelPerfect && camera->_projection == ECameraProjection::Orthographic) {
        const CameraViewFraming framing =
            resolveCameraViewFraming(*camera, outputExtentPx.x / outputExtentPx.y, outputExtentPx);
        if (framing.halfHeight > 0.0f) {
            return framing.halfWidth / framing.halfHeight;
        }
    }
    return outputExtentPx.x / outputExtentPx.y;
}

glm::mat4 cameraViewFromOwner(Entity* owner)
{
    if (FOwnerWorldPose pose; resolveOwnerWorldPose(owner, pose)) {
        return viewFromPose(pose);
    }
    return glm::mat4(1.0f);
}

} // namespace ya
