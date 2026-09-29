#include "ECS/Component/2D/TilemapComponent.h"

#include "Core/Reflection/ReflectionSerializer.h"

#include <gtest/gtest.h>

namespace ya
{

// The map is scene data: layers must survive a serialize/deserialize round
// trip with cell values, order and z offsets intact.
TEST(TilemapComponentTest, RoundTripsLayers)
{
    TilemapComponent map;
    map.tileset.setPath("Content:Tilesets/town.yatileset.json");
    map.cellSize = glm::vec2(1.0f, 1.0f);
    map.width    = 3;
    map.height   = 2;
    map.layer    = 2;

    TilemapLayer ground;
    ground.name    = "Ground";
    ground.zOffset = 0.0f;
    ground.cells   = {1, 1, 1, 1, 0, 1};
    TilemapLayer overlay;
    overlay.name    = "Overlay";
    overlay.zOffset = 0.2f;
    overlay.cells   = {0, 0, 0, 5, 5, 0};
    map.layers = {ground, overlay};

    const nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(map);

    TilemapComponent loaded;
    ReflectionSerializer::deserializeByRuntimeReflection(loaded, json, "TilemapComponent");

    // AssetRefBase::normalizePath folds the mount-style Content: prefix into a
    // slash path on deserialize, so the stored path reads back normalized.
    EXPECT_EQ(loaded.tileset.getPath(), "Content/Tilesets/town.yatileset.json");
    EXPECT_EQ(loaded.width, 3);
    EXPECT_EQ(loaded.height, 2);
    EXPECT_EQ(loaded.layer, 2);
    ASSERT_EQ(loaded.layers.size(), 2u);
    EXPECT_EQ(loaded.layers[0].name, "Ground");
    EXPECT_FLOAT_EQ(loaded.layers[0].zOffset, 0.0f);
    EXPECT_EQ(loaded.layers[0].cells, ground.cells);
    EXPECT_EQ(loaded.layers[1].name, "Overlay");
    EXPECT_FLOAT_EQ(loaded.layers[1].zOffset, 0.2f);
    EXPECT_EQ(loaded.layers[1].cells, overlay.cells);
    EXPECT_EQ(loaded.cellAt(1, 1, 0), 0);
    EXPECT_EQ(loaded.cellAt(0, 1, 1), 5);
    EXPECT_EQ(loaded.cellAt(9, 9, 0), -1);
}

} // namespace ya

