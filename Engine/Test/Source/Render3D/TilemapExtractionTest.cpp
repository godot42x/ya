#include "Scene2D/TilemapComponent.h"
#include "Render3D/Common/TilemapExtraction.h"
#include "Render3D/Common/TilemapExtraction.h"

#include "Render3D/RenderFrameData.h"
#include "RHI/Core/Texture.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

Tileset makeTileset()
{
    Tileset tileset;
    tileset.atlas.fromPath("Content:Textures/tiny_town.png");
    tileset.tileWidth  = 16;
    tileset.tileHeight = 16;
    tileset.margin     = 0;
    tileset.spacing    = 0;
    tileset.columns    = 12;
    return tileset;
}

TilemapComponent makeMap(int32_t width, int32_t height, std::vector<int32_t> cells)
{
    TilemapComponent map;
    map.width    = width;
    map.height   = height;
    map.cellSize = glm::vec2(1.0f, 1.0f);
    TilemapLayer layer;
    layer.name    = "Ground";
    layer.zOffset = 0.0f;
    layer.cells   = std::move(cells);
    map.layers.push_back(std::move(layer));
    return map;
}

} // namespace

// A view-sized caller narrows the range instead of expanding the whole map:
// only cells inside [min, max] become candidates.
TEST(TilemapExtractionTest, OnlyVisibleCellsBecomeCandidates)
{
    const Tileset tileset = makeTileset();
    // 4x3, every cell holds tile 0 (value 1).
    TilemapComponent map = makeMap(4, 3, std::vector<int32_t>(12, 1));
    const TextureBinding binding{};

    TilemapExtractionInput in;
    in.map                = &map;
    in.tileset            = &tileset;
    in.world              = glm::mat4(1.0f);
    in.entityId           = 42;
    in.atlas              = &binding;
    in.textureWidth       = 192;
    in.textureHeight      = 176;
    in.bHasVisibleRange   = true;
    in.minX = 1;
    in.minY = 1;
    in.maxX = 2;
    in.maxY = 2;

    std::vector<WorldSpriteCandidate> out;
    appendTilemapCandidates(in, out);

    ASSERT_EQ(out.size(), 4u);
    // Cell (1, 1) centers on (1.5, 1.5) under the identity transform.
    EXPECT_FLOAT_EQ(out[0].worldCenter.x, 1.5f);
    EXPECT_FLOAT_EQ(out[0].worldCenter.y, 1.5f);
    EXPECT_FLOAT_EQ(out[0].worldCenter.z, 0.0f);
    EXPECT_EQ(out[0].entityId, 42u);
    // Tile 0 is the top-left 16x16 window, shrunk by half a texel per side
    // because the packed atlas has no gutter.
    EXPECT_FLOAT_EQ(out[0].uvRect.x, 0.5f / 192.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.z, 15.5f / 192.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.y, 0.5f / 176.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.w, 15.5f / 176.0f);
    EXPECT_FALSE(out[0].bTranslucent);
}

// Empty cells and tile windows outside the atlas pixels produce nothing:
// extraction never invents pixels for authoring mistakes.
TEST(TilemapExtractionTest, EmptyCellsAreSkipped)
{
    const Tileset tileset = makeTileset();
    // 3x1: empty, tile 0, then tile 199 whose row sits past the 176px atlas.
    TilemapComponent map = makeMap(3, 1, {0, 1, 200});
    const TextureBinding binding{};

    TilemapExtractionInput in;
    in.map           = &map;
    in.tileset       = &tileset;
    in.world         = glm::mat4(1.0f);
    in.entityId      = 7;
    in.atlas         = &binding;
    in.textureWidth  = 192;
    in.textureHeight = 176;

    std::vector<WorldSpriteCandidate> out;
    appendTilemapCandidates(in, out);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].worldCenter.x, 1.5f);
    EXPECT_FLOAT_EQ(out[0].worldCenter.y, 0.5f);
    EXPECT_EQ(out[0].entityId, 7u);
}

} // namespace ya

