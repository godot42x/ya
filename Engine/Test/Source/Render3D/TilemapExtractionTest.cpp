#include "Scene2D/SpriteDrawOrder.h"
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
    EXPECT_EQ(out[0].drawKey.entityId, 42u);
    EXPECT_EQ(out[0].drawKey.layer, 0);
    EXPECT_EQ(out[0].drawKey.order, 0);
    EXPECT_EQ(out[0].drawKey.ySortRank, 0);
    EXPECT_FLOAT_EQ(out[0].sortPointY, 1.5f);
    // Tile 0 is the top-left 16x16 window, shrunk by half a texel per side
    // because the packed atlas has no gutter.
    EXPECT_FLOAT_EQ(out[0].uvRect.x, 0.5f / 192.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.z, 15.5f / 192.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.y, 0.5f / 176.0f);
    EXPECT_FLOAT_EQ(out[0].uvRect.w, 15.5f / 176.0f);
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
    EXPECT_EQ(out[0].drawKey.entityId, 7u);
}

TEST(TilemapExtractionTest, LayerOffsetAndYSortFeedThePainterKey)
{
    const Tileset tileset = makeTileset();
    TilemapComponent map = makeMap(2, 2, {1, 1, 0, 1});
    map.layer = 3;
    map.layers[0].bYSort = false;
    TilemapLayer overlay;
    overlay.name = "Overlay";
    overlay.layerOffset = 1;
    overlay.bYSort = true;
    overlay.zOffset = 0.2f;
    overlay.cells = {0, 1, 1, 0};
    map.layers.push_back(std::move(overlay));
    const TextureBinding binding{};

    TilemapExtractionInput in;
    in.map           = &map;
    in.tileset       = &tileset;
    in.world         = glm::mat4(1.0f);
    in.entityId      = 9;
    in.atlas         = &binding;
    in.textureWidth  = 192;
    in.textureHeight = 176;

    std::vector<WorldSpriteCandidate> out;
    appendTilemapCandidates(in, out);

    // Ground: (0,0), (1,0), (1,1). Overlay: (1,0), (0,1).
    ASSERT_EQ(out.size(), 5u);
    EXPECT_EQ(out[0].drawKey.layer, 3);
    EXPECT_EQ(out[0].drawKey.order, 0);
    EXPECT_EQ(out[0].drawKey.ySortRank, 0);
    EXPECT_FLOAT_EQ(out[0].drawKey.yKey, 0.0f);
    EXPECT_EQ(out[0].drawKey.sequence, 0u);
    EXPECT_EQ(out[2].drawKey.sequence, 2u);

    EXPECT_EQ(out[3].drawKey.layer, 4);
    EXPECT_EQ(out[3].drawKey.order, 1);
    EXPECT_EQ(out[3].drawKey.ySortRank, 1);
    EXPECT_FLOAT_EQ(out[3].sortPointY, 0.5f);
    EXPECT_FLOAT_EQ(out[3].drawKey.yKey, -0.5f);
    EXPECT_EQ(out[3].drawKey.sequence, 0u);
    EXPECT_FLOAT_EQ(out[4].sortPointY, 1.5f);
    EXPECT_FLOAT_EQ(out[4].drawKey.yKey, -1.5f);
    EXPECT_EQ(out[4].drawKey.sequence, 1u);
    EXPECT_TRUE(spriteDrawsBefore(out[0].drawKey, out[3].drawKey));
}

} // namespace ya

