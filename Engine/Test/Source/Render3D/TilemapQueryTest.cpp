#include "ECS/Component/2D/TilemapComponent.h"

#include "Core/Common/Tileset.h"
#include "ECS/Entity.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

// A tileset whose tile 4 is a wall, mirroring tiny_town's tree canopy index
// in the example project (solid list = {4, 16} there).
TilesetRef makeWallTileset()
{
    static std::shared_ptr<Tileset> shared = [] {
        auto tileset         = std::make_shared<Tileset>();
        tileset->columns     = 12;
        tileset->tileWidth   = 16;
        tileset->tileHeight  = 16;
        tileset->solidTiles  = {4};
        return tileset;
    }();

    TilesetRef ref("Content:Tilesets/wall.yatileset.json");
    ref._cached       = shared;
    ref._resolveState = EAssetResolveState::Ready;
    return ref;
}

} // namespace

// The movement query face answers "can the player stand here?" without a
// physics world. A solid tile blocks; the map edge blocks like a wall.
TEST(TilemapQueryTest, SolidTileBlocks)
{
    TilemapComponent map;
    map.width      = 4;
    map.height     = 3;
    map.cellSize   = glm::vec2(1.0f, 1.0f);
    map._editWidth = 4;
    map._editHeight = 3;
    map.tileset    = makeWallTileset();
    map.layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(12, 1)});

    ASSERT_FALSE(map.isSolid(0, 0));
    ASSERT_TRUE(map.setCell(1, 1, 0, 5)); // tile 4 + 1 = the wall tile
    EXPECT_TRUE(map.isSolid(1, 1));
    EXPECT_FALSE(map.isSolid(2, 1));

    // A layer with no tileset resolved cannot answer "solid": an unloaded
    // tileset yields no blocking tiles rather than blocking everything.
    TilemapComponent unresolved;
    unresolved.width      = 2;
    unresolved.height     = 2;
    unresolved.cellSize   = glm::vec2(1.0f, 1.0f);
    unresolved._editWidth = 2;
    unresolved._editHeight = 2;
    unresolved.layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(4, 1)});
    EXPECT_FALSE(unresolved.isSolid(0, 0));
}

// Off-map is solid, so a player walking at the edge stops instead of stepping
// into nothing; that is also what lets the camera clamp use the same bounds.
TEST(TilemapQueryTest, OutOfBoundsIsSolid)
{
    TilemapComponent map;
    map.width      = 3;
    map.height     = 3;
    map.cellSize   = glm::vec2(1.0f, 1.0f);
    map._editWidth = 3;
    map._editHeight = 3;
    map.tileset    = makeWallTileset();
    map.layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(9, 1)});

    EXPECT_TRUE(map.isSolid(-1, 0));
    EXPECT_TRUE(map.isSolid(0, -1));
    EXPECT_TRUE(map.isSolid(3, 0));
    EXPECT_TRUE(map.isSolid(0, 3));
    EXPECT_TRUE(map.containsCell(2, 2));
    EXPECT_FALSE(map.containsCell(3, 2));
}

// World <-> cell goes through the owning transform, so a map placed at
// (-5, -2) reports the cells a player standing at world (0, 0) expects.
TEST(TilemapQueryTest, WorldToCellUsesTheOwnerTransform)
{
    Scene scene("TilemapQuery");
    Entity* entity = scene.createNode3D("Map")->getEntity();
    ASSERT_NE(entity, nullptr);
    auto* transform = entity->getComponent<TransformComponent>();
    ASSERT_NE(transform, nullptr);
    transform->setPosition(glm::vec3(-5.0f, -2.0f, 0.01f));

    auto* map = entity->addComponent<TilemapComponent>();
    ASSERT_NE(map, nullptr);
    map->width      = 6;
    map->height     = 5;
    map->cellSize   = glm::vec2(1.0f, 1.0f);
    map->_editWidth = 6;
    map->_editHeight = 5;
    map->tileset    = makeWallTileset();
    map->layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(30, 1)});

    // Player start (0, 0.25) is cell (5, 2) in the hand-written scene.
    const glm::vec2 cell = map->worldToCell(glm::vec2(0.0f, 0.25f));
    EXPECT_FLOAT_EQ(cell.x, 5.0f);
    EXPECT_FLOAT_EQ(cell.y, 2.0f);

    // One cell left of the map origin is outside it.
    const glm::vec2 outside = map->worldToCell(glm::vec2(-5.5f, -2.0f));
    EXPECT_FLOAT_EQ(outside.x, -1.0f);
    EXPECT_FLOAT_EQ(outside.y, 0.0f);

    // Round trip: the centre of cell (5, 2) maps back to itself.
    const glm::vec2 back = map->worldToCell(map->cellToWorld(5, 2));
    EXPECT_FLOAT_EQ(back.x, 5.0f);
    EXPECT_FLOAT_EQ(back.y, 2.0f);

    const glm::vec4 bounds = map->bounds();
    EXPECT_FLOAT_EQ(bounds.x, -5.0f);
    EXPECT_FLOAT_EQ(bounds.y, -2.0f);
    EXPECT_FLOAT_EQ(bounds.z, 1.0f);
    EXPECT_FLOAT_EQ(bounds.w, 3.0f);
}

// Without an owning entity the query face falls back to identity, so the
// pure-data form keeps working (extraction and edit tests build maps like
// this).
TEST(TilemapQueryTest, OwnerlessMapUsesIdentityTransform)
{
    TilemapComponent map;
    map.width      = 2;
    map.height     = 2;
    map.cellSize   = glm::vec2(2.0f, 2.0f);
    map._editWidth = 2;
    map._editHeight = 2;
    map.tileset    = makeWallTileset();
    map.layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(4, 1)});

    const glm::vec2 cell = map.worldToCell(glm::vec2(3.0f, 1.0f));
    EXPECT_FLOAT_EQ(cell.x, 1.0f);
    EXPECT_FLOAT_EQ(cell.y, 0.0f);
    EXPECT_EQ(map.bounds(), glm::vec4(0.0f, 0.0f, 4.0f, 4.0f));
}

} // namespace ya
