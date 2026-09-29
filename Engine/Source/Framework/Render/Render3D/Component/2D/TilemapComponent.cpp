#include "ECS/Component/2D/TilemapComponent.h"

#include "ECS/Entity.h"
#include "ECS/Systems/TransformSystem.h"
#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Texture.h"
#include "Scene3D/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ya
{

namespace
{

// Tile window in image space (v = 0 is the top row, the same convention as
// Sprite2DComponent::uvRect). A packed atlas has no gutter between tiles,
// so the window shrinks by half a texel on every side: with Nearest
// sampling that keeps a tile from bleeding its neighbor in.
glm::vec4 tileUvRect(const Tileset& tileset, int32_t tile, uint32_t textureWidth, uint32_t textureHeight,
                     bool& outOfAtlas)
{
    outOfAtlas     = true;
    const int32_t col = tile % tileset.columns;
    const int32_t row = tile / tileset.columns;
    const int32_t px  = tileset.margin + col * (tileset.tileWidth + tileset.spacing);
    const int32_t py  = tileset.margin + row * (tileset.tileHeight + tileset.spacing);
    if (px < 0 || py < 0 || px + tileset.tileWidth > static_cast<int32_t>(textureWidth) ||
        py + tileset.tileHeight > static_cast<int32_t>(textureHeight)) {
        return glm::vec4(0.0f);
    }
    outOfAtlas = false;

    const float texW = static_cast<float>(textureWidth);
    const float texH = static_cast<float>(textureHeight);
    float u0 = static_cast<float>(px) / texW;
    float u1 = static_cast<float>(px + tileset.tileWidth) / texW;
    float v0 = static_cast<float>(py) / texH;
    float v1 = static_cast<float>(py + tileset.tileHeight) / texH;
    if (tileset.margin == 0 && tileset.spacing == 0) {
        u0 += 0.5f / texW;
        u1 -= 0.5f / texW;
        v0 += 0.5f / texH;
        v1 -= 0.5f / texH;
    }
    return glm::vec4(u0, v0, u1, v1);
}

} // namespace

int32_t TilemapComponent::cellAt(int32_t x, int32_t y, size_t layerIndex) const
{
    if (x < 0 || y < 0 || x >= width || y >= height || layerIndex >= layers.size()) {
        return -1;
    }
    const std::vector<int32_t>& cells = layers[layerIndex].cells;
    const int32_t index = cellIndex(x, y);
    if (index < 0 || index >= static_cast<int32_t>(cells.size())) {
        return -1;
    }
    return cells[static_cast<size_t>(index)];
}

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

void appendTilemapCandidates(const TilemapExtractionInput& in, std::vector<WorldSpriteCandidate>& out)
{
    if (!in.map || !in.tileset || !in.atlas) {
        return;
    }
    const TilemapComponent& map     = *in.map;
    const Tileset&          tileset = *in.tileset;
    if (!map.isValid() || in.textureWidth == 0 || in.textureHeight == 0) {
        return;
    }
    if (tileset.tileWidth <= 0 || tileset.tileHeight <= 0 || tileset.columns <= 0) {
        return;
    }

    int32_t minX = 0;
    int32_t minY = 0;
    int32_t maxX = map.width - 1;
    int32_t maxY = map.height - 1;
    if (in.bHasVisibleRange) {
        minX = std::max(minX, in.minX);
        minY = std::max(minY, in.minY);
        maxX = std::min(maxX, in.maxX);
        maxY = std::min(maxY, in.maxY);
        if (minX > maxX || minY > maxY) {
            return;
        }
    }

    const glm::vec3 axisX = glm::vec3(in.world[0]) * map.cellSize.x;
    const glm::vec3 axisY = glm::vec3(in.world[1]) * map.cellSize.y;

    for (size_t layerIndex = 0; layerIndex < map.layers.size(); ++layerIndex) {
        const TilemapLayer& layer = map.layers[layerIndex];
        if (static_cast<int32_t>(layer.cells.size()) != map.width * map.height) {
            continue;
        }
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t x = minX; x <= maxX; ++x) {
                const int32_t value = layer.cells[static_cast<size_t>(map.cellIndex(x, y))];
                if (value <= 0) {
                    continue;
                }
                bool      outOfAtlas = false;
                const glm::vec4 uv =
                    tileUvRect(tileset, value - 1, in.textureWidth, in.textureHeight, outOfAtlas);
                if (outOfAtlas) {
                    continue;
                }
                const glm::vec3 local((static_cast<float>(x) + 0.5f) * map.cellSize.x,
                                      (static_cast<float>(y) + 0.5f) * map.cellSize.y, layer.zOffset);

                WorldSpriteCandidate candidate{};
                candidate.worldCenter = glm::vec3(in.world * glm::vec4(local, 1.0f));
                candidate.axisX       = axisX;
                candidate.axisY       = axisY;
                candidate.uvRect      = uv;
                candidate.tint        = glm::vec4(1.0f);
                candidate.texture     = *in.atlas;
                candidate.entityId    = in.entityId;
                candidate.layer       = map.layer;
                candidate.sortOrder   = static_cast<int32_t>(layerIndex);
                candidate.bTranslucent = false;
                out.push_back(candidate);
            }
        }
    }
}

void TilemapComponent::onEdit()
{
    // The Inspector writes width/height as plain fields; without this the
    // layers would keep their old cell count and extraction would skip them
    // as mismatched. The previous row width comes from the transient edit
    // dims, so widening 6 -> 10 keeps the 6 left columns exactly.
    if (!isValid()) {
        return;
    }
    const int32_t oldW = _editWidth;
    const int32_t oldH = _editHeight;
    const auto    want = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (TilemapLayer& layer : layers) {
        std::vector<int32_t> next(want, 0);
        if (oldW > 0 && oldH > 0 &&
            layer.cells.size() == static_cast<size_t>(oldW) * static_cast<size_t>(oldH)) {
            const int32_t copyW = std::min(width, oldW);
            const int32_t copyH = std::min(height, oldH);
            for (int32_t y = 0; y < copyH; ++y) {
                for (int32_t x = 0; x < copyW; ++x) {
                    next[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                        layer.cells[static_cast<size_t>(y) * static_cast<size_t>(oldW) + static_cast<size_t>(x)];
                }
            }
        }
        else {
            // Layer data never matched a known grid (hand-written mismatch):
            // keep the linear prefix instead of inventing a layout.
            const size_t keep = std::min(layer.cells.size(), want);
            for (size_t i = 0; i < keep; ++i) {
                next[i] = layer.cells[i];
            }
        }
        layer.cells.swap(next);
    }
    _editWidth  = width;
    _editHeight = height;
}

void TilemapComponent::onPostSerialize()
{
    _editWidth  = width;
    _editHeight = height;
}

void TilemapComponent::resize(int32_t newWidth, int32_t newHeight)
{
    if (newWidth <= 0 || newHeight <= 0) {
        return;
    }
    if (newWidth == width && newHeight == height && _editWidth == width && _editHeight == height) {
        return;
    }
    width  = newWidth;
    height = newHeight;
    onEdit();
}

bool TilemapComponent::setCell(int32_t x, int32_t y, size_t layerIndex, int32_t value)
{
    if (!isValid() || layerIndex >= layers.size()) {
        return false;
    }
    std::vector<int32_t>& cells = layers[layerIndex].cells;
    if (cells.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return false;
    }
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    cells[static_cast<size_t>(cellIndex(x, y))] = value;
    return true;
}

int32_t TilemapComponent::fillRect(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY,
                                   size_t layerIndex, int32_t value)
{
    if (!isValid() || layerIndex >= layers.size()) {
        return 0;
    }
    std::vector<int32_t>& cells = layers[layerIndex].cells;
    if (cells.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
        return 0;
    }
    const int32_t x0 = std::max(minX, 0);
    const int32_t y0 = std::max(minY, 0);
    const int32_t x1 = std::min(maxX, width - 1);
    const int32_t y1 = std::min(maxY, height - 1);
    int32_t       written = 0;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            cells[static_cast<size_t>(cellIndex(x, y))] = value;
            ++written;
        }
    }
    return written;
}

} // namespace ya
