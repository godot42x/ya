#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include "Scene3D/TransformComponent.h"

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

} // namespace

glm::mat4 CameraComponent::getProjection(float outputAspect) const
{
    const float aspect = _fixedAspectRatio ? _aspectRatio : outputAspect;
    if (_projection == ECameraProjection::Orthographic) {
        const float halfHeight = _orthoHalfHeight;
        const float halfWidth  = halfHeight * aspect;
        return FMath::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, _nearClip, _farClip);
    }
    return FMath::perspective(glm::radians(_fov), aspect, _nearClip, _farClip);
}

glm::mat4 cameraViewFromOwner(Entity* owner)
{
    if (FOwnerWorldPose pose; resolveOwnerWorldPose(owner, pose)) {
        const glm::vec3 forward = pose.rotation * FMath::Vector::WorldForward;
        const glm::vec3 up      = pose.rotation * FMath::Vector::WorldUp;
        return FMath::lookAt(pose.position, pose.position + forward, up);
    }
    return glm::mat4(1.0f);
}

} // namespace ya
