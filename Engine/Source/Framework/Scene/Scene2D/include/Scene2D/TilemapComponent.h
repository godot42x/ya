#pragma once

#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"
#include "Core/Common/Tileset.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ya
{

// One paint layer of a tilemap: a dense grid of cell values, a depth offset
// against 3D geometry, and this sub-layer's painter-key fields. Cell value 0
// is empty, any other value is (tile index + 1) into the Tileset. Cells are
// row-major
// from the bottom-left: cells[y * width + x], so the first row in the
// serialized array is the world-bottom row.
struct YA_SCENE_2D_API TilemapLayer
{
    YA_REFLECT_BEGIN(TilemapLayer)
    YA_REFLECT_FIELD(name)
    YA_REFLECT_FIELD(zOffset)
    YA_REFLECT_FIELD(bYSort)
    YA_REFLECT_FIELD(layerOffset)
    YA_REFLECT_FIELD(cells)
    YA_REFLECT_END()

    std::string name;
    /// Depth against 3D opaque geometry. It does not order this layer against
    /// other sprites or tiles; that is the painter key.
    float       zOffset = 0.0f;
    /// Sort each tile by its cell centre's world y. Off, the y term is zero
    /// and the row-major extraction order is the tiebreak.
    bool        bYSort = false;
    /// Added to `TilemapComponent::layer` for this sub-layer's painter layer.
    /// Zero stays on the component layer. A higher offset paints later, so an
    /// overlay can cover y-sorted actors that share the component layer.
    int32_t     layerOffset = 0;
    std::vector<int32_t> cells;
};

// Authored tile grid on a Node3D. Position, rotation and scale stay on
// TransformComponent; this type only says which tile sits on which cell.
// Rendering expands every non-empty cell into a WorldSpriteCandidate
// (Render3D/Common/TilemapExtraction.h) and reuses Sprite2DStage, so there is
// no tilemap pass: a tile is drawn exactly like a 1x1 sprite whose uvRect is
// the tile window.
//
// Cell (x, y) centers on ((x + 0.5) * cellSize.x, (y + 0.5) * cellSize.y,
// layer.zOffset) in tilemap-local space, transformed by the entity world
// matrix. zOffset is only the depth tested against 3D opaque geometry.
// Painter order uses `layer + layerOffset`, then the sub-layer index, and
// the cell centre's world y when that sub-layer's y-sort is on.
struct YA_SCENE_2D_API TilemapComponent : public IComponent
{
    YA_REFLECT_BEGIN(TilemapComponent, IComponent)
    YA_REFLECT_FIELD(tileset)
    YA_REFLECT_FIELD(cellSize)
    YA_REFLECT_FIELD(width)
    YA_REFLECT_FIELD(height)
    YA_REFLECT_FIELD(layers)
    YA_REFLECT_FIELD(layer)
    YA_REFLECT_METHOD(worldToCell, .tooltip("World position to cell coordinates"))
    YA_REFLECT_METHOD(cellToWorld, .tooltip("Cell centre in world space"))
    YA_REFLECT_METHOD(isSolid, .tooltip("True when the cell is outside the map or holds a solid tile"))
    YA_REFLECT_METHOD(bounds, .tooltip("World (minX, minY, maxX, maxY) of the whole map"))
    YA_REFLECT_END()

    TilesetRef                 tileset;
    glm::vec2                  cellSize{1.0f, 1.0f};
    int32_t                    width  = 0;
    int32_t                    height = 0;
    std::vector<TilemapLayer>  layers;
    // Draw group shared with Sprite2DComponent::layer; a tile sorts against
    // sprites by this, then by its layer index inside the map.
    int32_t                    layer = 0;

    // Transient dims at the last reconcile (not serialized). The Inspector
    // writes width/height as plain fields, so onEdit recovers the previous
    // row width from here instead of guessing it from the cell count.
    int32_t _editWidth  = 0;
    int32_t _editHeight = 0;

    [[nodiscard]] bool isValid() const
    {
        return width > 0 && height > 0 && cellSize.x > 0.0f && cellSize.y > 0.0f;
    }
    [[nodiscard]] int32_t cellIndex(int32_t x, int32_t y) const { return y * width + x; }
    // Cell value at (x, y) on layer layerIndex, or -1 when out of bounds
    // or the layer has no cell data.
    [[nodiscard]] int32_t cellAt(int32_t x, int32_t y, size_t layerIndex) const;

    // --- Query face (D1) ---------------------------------------------------
    // Movement asks this component instead of a physics world: "is this
    // place walkable?". The answers are in tilemap-local space, so nothing
    // here needs a Scene or a world matrix, and a later AABB/Box2D/Jolt
    // implementation can replace them behind the same four names without
    // gameplay scripts changing.

    /// World position to cell: `floor(local / cellSize)` after the inverse
    /// world matrix. The cell comes back even outside the map; `isSolid`
    /// answers "is it on the map at all?" for every out-of-range cell.
    [[nodiscard]] glm::vec2 worldToCell(const glm::vec2& world) const;
    /// Cell centre in tilemap-local space: ((x + 0.5) * cellSize.x, ...).
    [[nodiscard]] glm::vec2 cellToWorld(int32_t x, int32_t y) const;
    /// True when (x, y) is outside the map or any layer's tile there is
    /// solid in the tileset. Outside is solid so a map edge stops the player
    /// exactly like a wall does.
    [[nodiscard]] bool isSolid(int32_t x, int32_t y) const;
    /// World-space (minX, minY, maxX, maxY) of the whole map. A rotated
    /// tilemap reports the box of its corners, which is what a camera clamp
    /// wants.
    [[nodiscard]] glm::vec4 bounds() const;
    /// True while (x, y) is a cell of this map.
    [[nodiscard]] bool containsCell(int32_t x, int32_t y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    void onEdit() override;
    void onPostSerialize() override;
    // The layers array is omitted as a whole, so a default bYSort / layerOffset
    // inside a saved layer would otherwise be written. Drop just those two.
    void serializeCustom(nlohmann::json& out) const override;
    // Resize keeping the overlapping cells at the same origin; new cells
    // are empty. Non-positive sizes are ignored.
    void resize(int32_t newWidth, int32_t newHeight);
    // Write one cell; false when out of bounds or the layer has no data.
    bool setCell(int32_t x, int32_t y, size_t layerIndex, int32_t value);
    // Fill the clamped range on one layer; returns cells written.
    int32_t fillRect(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, size_t layerIndex, int32_t value);
};

} // namespace ya
