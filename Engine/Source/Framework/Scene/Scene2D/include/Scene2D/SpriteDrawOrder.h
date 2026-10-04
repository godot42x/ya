#pragma once

#include <cstdint>

namespace ya
{

/// Painter key for one scene sprite or one tile. Every field ascends, and the
/// later key is drawn on top. `Transform.z` is not in the key: it only
/// depth-tests against 3D opaque geometry.
///
/// This is the only comparison. Extraction sorts with it, and 2D picking
/// keeps the hit that draws last.
struct SpriteDrawKey
{
    int32_t layer = 0;
    /// 0 when y-sort is off, 1 when it is on. Unsorted objects paint first
    /// inside a layer, so a constant y term cannot interleave with them.
    int32_t ySortRank = 0;
    /// `-sortY` when y-sort is on, otherwise 0. `sortY` is the sort point's
    /// world y after texel snap (sprite pivot, or the tile's cell centre).
    float    yKey     = 0.0f;
    int32_t  order    = 0;
    uint32_t entityId = 0;
    /// Row-major extraction order of a tile. Sprites leave this at 0.
    uint32_t sequence = 0;
};

/// True when `lhs` is painted before `rhs`.
[[nodiscard]] inline bool spriteDrawsBefore(const SpriteDrawKey& lhs, const SpriteDrawKey& rhs)
{
    if (lhs.layer != rhs.layer) {
        return lhs.layer < rhs.layer;
    }
    if (lhs.ySortRank != rhs.ySortRank) {
        return lhs.ySortRank < rhs.ySortRank;
    }
    if (lhs.yKey != rhs.yKey) {
        return lhs.yKey < rhs.yKey;
    }
    if (lhs.order != rhs.order) {
        return lhs.order < rhs.order;
    }
    if (lhs.entityId != rhs.entityId) {
        return lhs.entityId < rhs.entityId;
    }
    return lhs.sequence < rhs.sequence;
}

/// `sortY` is the world y of the sort point. Y-sort off forces rank 0 and yKey 0.
[[nodiscard]] inline SpriteDrawKey makeSpriteDrawKey(int32_t  layer,
                                                     bool     bYSort,
                                                     float    sortY,
                                                     int32_t  order,
                                                     uint32_t entityId,
                                                     uint32_t sequence)
{
    SpriteDrawKey key;
    key.layer     = layer;
    key.ySortRank = bYSort ? 1 : 0;
    key.yKey      = bYSort ? -sortY : 0.0f;
    key.order     = order;
    key.entityId  = entityId;
    key.sequence  = sequence;
    return key;
}

} // namespace ya
