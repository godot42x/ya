#pragma once

#include "Core/Common/TextureSlot.h"
#include "ECS/Component.h"

namespace ya
{

/// Authored scene sprite. Position, rotation, and scale stay on
/// `TransformComponent`; this type does not copy them and does not hold a
/// material, descriptor, or command buffer.
///
/// The quad is the entity's local XY rectangle, centered on the origin.
/// `size.x` is the unscaled width along local +X, `size.y` the height along
/// local +Y, and the face looks along local +Z. `Transform` scale still
/// applies through the world matrix.
///
/// Draw policy for the later world-sprite pass: an opaque sprite
/// (`tint.a` >= 1) depth-tests and depth-writes, so its world position
/// occludes and is occluded by scene geometry. A translucent sprite
/// depth-tests, does not write depth, and sorts by `layer`, then
/// `sortOrder`, then view depth. `uvRect` and the flip flags are sampled
/// by that pass. A sprite whose texture is unset, still loading, or failed
/// is not drawn; there is no substitute image.
struct YA_RENDER_3D_API Sprite2DComponent : public IComponent
{
    YA_REFLECT_BEGIN(Sprite2DComponent, IComponent)
    YA_REFLECT_FIELD(bVisible, .script())
    YA_REFLECT_FIELD(image)
    YA_REFLECT_FIELD(size, .script())
    YA_REFLECT_FIELD(uvRect, .script())
    YA_REFLECT_FIELD(bFlipU, .script())
    YA_REFLECT_FIELD(bFlipV, .script())
    YA_REFLECT_FIELD(tint, .color().script())
    YA_REFLECT_FIELD(layer, .script())
    YA_REFLECT_FIELD(sortOrder, .script())
    YA_REFLECT_FIELD(pickId)
    YA_REFLECT_END()

    bool        bVisible  = true;
    TextureSlot image;
    glm::vec2   size{1.0f, 1.0f};
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
[[nodiscard]] YA_RENDER_3D_API bool spriteIsDrawable(const Sprite2DComponent& sprite);

} // namespace ya
