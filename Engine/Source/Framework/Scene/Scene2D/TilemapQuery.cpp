#include "Scene2D/TilemapComponent.h"

#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "Scene3D/TransformComponent.h"

#include <cmath>
#include <limits>

namespace ya
{

namespace
{

// The query face works in world space, so it needs the owning entity's world
// matrix. A component the scene has not adopted yet (no owner, no transform)
// falls back to the identity: a tilemap at the origin is the sane reading of
// "its transform is not known here", and it keeps the pure-data tests working
// without a Scene.
glm::mat4 ownerWorldMatrix(const TilemapComponent& map)
{
    Entity* owner = map.getOwner();
    if (!owner) {
        return glm::mat4(1.0f);
    }
    TransformComponent* transform =
        owner->hasComponent<TransformComponent>() ? owner->getComponent<TransformComponent>() : nullptr;
    if (!transform) {
        return glm::mat4(1.0f);
    }
    TransformSystem::computeWorldMatrix(transform);
    return transform->getTransform();
}

} // namespace

glm::vec2 TilemapComponent::worldToCell(const glm::vec2& world) const
{
    if (!isValid()) {
        return glm::vec2(0.0f);
    }
    const glm::mat4 inverse = glm::inverse(ownerWorldMatrix(*this));
    const glm::vec4 local   = inverse * glm::vec4(world.x, world.y, 0.0f, 1.0f);
    const float     x       = local.w != 0.0f ? local.x / local.w : local.x;
    const float     y       = local.w != 0.0f ? local.y / local.w : local.y;
    return glm::vec2(std::floor(x / cellSize.x), std::floor(y / cellSize.y));
}

glm::vec2 TilemapComponent::cellToWorld(int32_t x, int32_t y) const
{
    const glm::vec3 local((static_cast<float>(x) + 0.5f) * cellSize.x,
                          (static_cast<float>(y) + 0.5f) * cellSize.y, 0.0f);
    const glm::vec4 world = ownerWorldMatrix(*this) * glm::vec4(local, 1.0f);
    return glm::vec2(world.x, world.y);
}

bool TilemapComponent::isSolid(int32_t x, int32_t y) const
{
    // Off the map is solid: the edge of a painted map stops movement the same
    // way a painted wall does, so a player can never walk into nothing.
    if (!containsCell(x, y)) {
        return true;
    }
    const Tileset* tileset = this->tileset.get();
    if (!tileset) {
        return false;
    }
    for (size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex) {
        const int32_t value = cellAt(x, y, layerIndex);
        // Cell value is tile index + 1; 0 and -1 are "empty" and "no data".
        if (value > 0 && tileset->isSolidTile(value - 1)) {
            return true;
        }
    }
    return false;
}

glm::vec4 TilemapComponent::bounds() const
{
    const glm::mat4 world = ownerWorldMatrix(*this);
    // The map spans [0, width] x [0, height] in local space; its four corners
    // transformed give the axis-aligned world box. A rotated tilemap reports
    // the box of its corners, which is what a camera clamp wants.
    const glm::vec4 corners[4] = {
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        glm::vec4(static_cast<float>(width) * cellSize.x, 0.0f, 0.0f, 1.0f),
        glm::vec4(0.0f, static_cast<float>(height) * cellSize.y, 0.0f, 1.0f),
        glm::vec4(static_cast<float>(width) * cellSize.x, static_cast<float>(height) * cellSize.y, 0.0f, 1.0f),
    };
    glm::vec2 min(std::numeric_limits<float>::max());
    glm::vec2 max(std::numeric_limits<float>::lowest());
    for (const glm::vec4& corner : corners) {
        const glm::vec4 worldCorner = world * corner;
        const glm::vec2 point(worldCorner.x, worldCorner.y);
        min = glm::min(min, point);
        max = glm::max(max, point);
    }
    return glm::vec4(min.x, min.y, max.x, max.y);
}

} // namespace ya
