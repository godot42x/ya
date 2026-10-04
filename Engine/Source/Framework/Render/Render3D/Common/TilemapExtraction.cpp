#include "Render3D/Common/TilemapExtraction.h"

#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Texture.h"
#include "Scene2D/SpriteDrawOrder.h"

#include <algorithm>

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
        uint32_t sequence = 0;
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
                candidate.texture    = *in.atlas;
                candidate.sortPointY = candidate.worldCenter.y;
                candidate.drawKey    = makeSpriteDrawKey(map.layer + layer.layerOffset,
                                                         layer.bYSort,
                                                         candidate.sortPointY,
                                                         static_cast<int32_t>(layerIndex),
                                                         in.entityId,
                                                         sequence);
                ++sequence;
                out.push_back(candidate);
            }
        }
    }
}

} // namespace ya
