#include "PhysicsDebugDraw.h"

#include "ECS/Component/TransformComponent.h"
#include "Physics/PhysicsBodyComponent.h"
#include "UI/2D/Render2D.h"
#include "Scene/Scene.h"

namespace ya
{

void drawPhysicsCollisionDebug(Scene& scene)
{
    constexpr glm::vec4 kBoxColor   = {0.2f, 0.9f, 0.3f, 1.0f};
    constexpr glm::vec4 kSphereColor = {0.3f, 0.6f, 1.0f, 1.0f};

    for (auto&& [entity, transform, bodyComponent] :
         scene.getRegistry().view<TransformComponent, PhysicsBodyComponent>().each()) {
        (void)entity;

        // Mirror the body transform used by PhysicsSystem: position + rotation
        // from the entity transform; scale is not part of the v1 body shape.
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.getPosition()) *
                                glm::mat4_cast(glm::quat(glm::radians(transform.getRotation())));

        if (bodyComponent._shape == PhysicsBodyShape::Sphere) {
            Render2D::makeWireSphere(transform.getPosition(),
                                     PhysicsBodyComponent::kDefaultSphereRadius,
                                     kSphereColor);
        } else {
            Render2D::makeWireBox(model,
                                  glm::vec3(PhysicsBodyComponent::kDefaultBoxHalfExtent),
                                  kBoxColor);
        }
    }
}

} // namespace ya
