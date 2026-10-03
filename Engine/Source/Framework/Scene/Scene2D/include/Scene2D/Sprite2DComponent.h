#pragma once

#include "Core/Common/TextureSlot.h"
#include "ECS/Component.h"

namespace ya
{

/// Authored scene sprite. Position, rotation, and scale stay on
/// `TransformComponent`; this type does not copy them and does not hold a
/// material, descriptor, or command buffer.
///
/// The quad is the entity's local XY rectangle. Its pivot sits on the entity
/// origin: `pivot` is normalized, (0, 0) the bottom-left and (1, 1) the
/// top-right. The default (0.5, 0.5) centers the quad, so a scene saved
/// before the field renders unchanged. `size.x` is the unscaled width along
/// local +X, `size.y` the height along local +Y, and the face looks along
/// local +Z. `Transform` scale and rotation still apply through the world
/// matrix.
///
/// Draw policy for the later world-sprite pass: an opaque sprite
/// (`tint.a` >= 1) depth-tests and depth-writes, so its world position
/// occludes and is occluded by scene geometry. A translucent sprite
/// depth-tests, does not write depth, and sorts by `layer`, then
/// `sortOrder`, then view depth. `uvRect` and the flip flags are sampled
/// by that pass. A sprite whose texture is unset, still loading, or failed
/// is not drawn; there is no substitute image.
struct YA_SCENE_2D_API Sprite2DComponent : public IComponent
{
    YA_REFLECT_BEGIN(Sprite2DComponent, IComponent)
    YA_REFLECT_FIELD(bVisible)
    YA_REFLECT_FIELD(image)
    YA_REFLECT_FIELD(size)
    YA_REFLECT_FIELD(pivot)
    YA_REFLECT_FIELD(uvRect)
    YA_REFLECT_FIELD(bFlipU)
    YA_REFLECT_FIELD(bFlipV)
    YA_REFLECT_FIELD(tint, .color())
    YA_REFLECT_FIELD(layer)
    YA_REFLECT_FIELD(sortOrder)
    YA_REFLECT_FIELD(pickId)
    YA_REFLECT_END()

    bool        bVisible  = true;
    TextureSlot image;
    glm::vec2   size{1.0f, 1.0f};
    /// Normalized point of the quad that sits on the entity origin.
    /// (0, 0) is the bottom-left, (1, 1) the top-right. (0.5, 0.5) centers it.
    glm::vec2   pivot{0.5f, 0.5f};
    /// Atlas window in texture space: (u0, v0, u1, v1).
    glm::vec4   uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    bool        bFlipU = false;
    bool        bFlipV = false;
    glm::vec4   tint{1.0f};
    int32_t     layer     = 0;
    int32_t     sortOrder = 0;
    /// Optional authoring id for a later pick group. Zero means "this entity".
    int32_t     pickId = 0;
};

/// True only when the sprite should be drawn. Unset, pending, and failed
/// textures all return false; callers must not invent a second placeholder.
[[nodiscard]] YA_SCENE_2D_API bool spriteIsDrawable(const Sprite2DComponent& sprite);

/// Quad centre relative to the entity origin, in local units (authored size
/// applied; transform scale and rotation are not). Rendering shifts the world
/// centre by this offset through the world axes; picking tests the same local
/// rectangle. Default pivot returns zero, so the centre stays on the entity.
[[nodiscard]] inline glm::vec2 spriteQuadCenterOffset(const Sprite2DComponent& sprite)
{
    return {(0.5f - sprite.pivot.x) * sprite.size.x,
            (0.5f - sprite.pivot.y) * sprite.size.y};
}

} // namespace ya
