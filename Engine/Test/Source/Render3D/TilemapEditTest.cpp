#include "ECS/Component/2D/TilemapComponent.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

TilemapComponent makeMap(int32_t width, int32_t height)
{
    TilemapComponent map;
    map.width     = width;
    map.height    = height;
    map.cellSize  = glm::vec2(1.0f, 1.0f);
    map._editWidth  = width;
    map._editHeight = height;
    TilemapLayer layer;
    layer.name  = "Ground";
    layer.cells = std::vector<int32_t>(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    map.layers.push_back(layer);
    TilemapLayer overlay;
    overlay.name  = "Overlay";
    overlay.cells = std::vector<int32_t>(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    map.layers.push_back(overlay);
    return map;
}

} // namespace

// Inspector-style widening keeps the existing columns: the previous row
// width comes from the transient edit dims, not from guessing.
TEST(TilemapEditTest, ResizeKeepsExistingCells)
{
    TilemapComponent map = makeMap(3, 2);
    ASSERT_TRUE(map.setCell(0, 0, 0, 1));
    ASSERT_TRUE(map.setCell(2, 1, 0, 7));
    ASSERT_TRUE(map.setCell(1, 0, 1, 5));

    map.resize(5, 4);

    EXPECT_EQ(map.width, 5);
    EXPECT_EQ(map.height, 4);
    ASSERT_EQ(map.layers[0].cells.size(), 20u);
    EXPECT_EQ(map.cellAt(0, 0, 0), 1);
    EXPECT_EQ(map.cellAt(2, 1, 0), 7);
    EXPECT_EQ(map.cellAt(4, 3, 0), 0);
    EXPECT_EQ(map.cellAt(1, 0, 1), 5);
    EXPECT_EQ(map.cellAt(0, 3, 1), 0);

    map.resize(2, 1);
    EXPECT_EQ(map.cellAt(0, 0, 0), 1);
    EXPECT_EQ(map.cellAt(1, 0, 0), 0);
    EXPECT_EQ(map.cellAt(0, 0, 1), 0);
}

// Inspector field write path: width is assigned directly, then onEdit
// reconciles with the transient edit dims tracking the previous grid.
TEST(TilemapEditTest, InspectorWidthEditKeepsCells)
{
    TilemapComponent map = makeMap(3, 2);
    ASSERT_TRUE(map.setCell(0, 0, 0, 1));
    ASSERT_TRUE(map.setCell(2, 1, 0, 7));
    map.onPostSerialize();

    map.width = 5;
    map.onEdit();

    EXPECT_EQ(map.cellAt(0, 0, 0), 1);
    EXPECT_EQ(map.cellAt(2, 1, 0), 7);
    EXPECT_EQ(map.cellAt(4, 1, 0), 0);
}

// A fill stroke touches exactly one layer: sibling layers keep their cells.
TEST(TilemapEditTest, RectFillWritesOnlyTheActiveLayer)
{
    TilemapComponent map = makeMap(4, 4);
    ASSERT_TRUE(map.setCell(0, 0, 1, 9));

    // Partially out of bounds on purpose: only the 2x2 overlap lands.
    EXPECT_EQ(map.fillRect(2, 2, 5, 5, 0, 3), 4);
    EXPECT_EQ(map.cellAt(2, 2, 0), 3);
    EXPECT_EQ(map.cellAt(3, 3, 0), 3);
    EXPECT_EQ(map.cellAt(1, 1, 0), 0);
    EXPECT_EQ(map.cellAt(0, 0, 1), 9);

    EXPECT_EQ(map.fillRect(0, 0, 3, 3, 7, 3), 0);
    EXPECT_FALSE(map.setCell(9, 9, 0, 3));
}

} // namespace ya

