#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Scene2D/TilemapComponent.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace ya
{
namespace
{

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

std::string readRepoFile(const std::string& relativePath)
{
    std::ifstream file(relativePath, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace

// R1a acceptance, pinned without a GPU: the hand-written TilemapGround
// entity in Town.scene.json deserializes through the real reflection path
// with its layers, cells and tileset reference intact.
//
// NOTE: only the TilemapComponent sub-object goes through the reflection
// deserializer here. Deserializing a whole texture-bearing entity also
// resolves its TextureRefs, which needs the App context (mounted VFS and
// AssetManager); that path is covered by the runtime smoke, not by tests.
TEST(TilemapSceneTest, HandwrittenTownTilemapDeserializes)
{
    ensureReflectionReady();

    const std::string sceneText =
        readRepoFile("Example/2DRpgPrototype/Content/Scenes/Town.scene.json");
    ASSERT_FALSE(sceneText.empty());
    const nlohmann::json sceneJson = nlohmann::json::parse(sceneText);

    const nlohmann::json* tilemapJson = nullptr;
    const nlohmann::json* playerJson  = nullptr;
    for (const auto& entityJson : sceneJson["entities"]) {
        if (entityJson["name"] == "TilemapGround") {
            tilemapJson = &entityJson;
        }
        if (entityJson["name"] == "Player") {
            playerJson = &entityJson;
        }
    }
    ASSERT_NE(tilemapJson, nullptr);
    ASSERT_NE(playerJson, nullptr);

    TilemapComponent map;
    ReflectionSerializer::deserializeByRuntimeReflection(
        map, (*tilemapJson)["components"]["TilemapComponent"], "TilemapComponent");

    EXPECT_TRUE(map.isValid());
    EXPECT_EQ(map.width, 32);
    EXPECT_EQ(map.height, 20);
    // Reflection deserialize normalizes the written mount-style form the same
    // way TextureRef does, so the stored path reads back as a slash path.
    EXPECT_EQ(map.tileset.getPath(), "Content/Tilesets/town.yatileset.json");
    ASSERT_EQ(map.layers.size(), 3u);
    EXPECT_EQ(map.layers[0].name, "Ground");
    EXPECT_FLOAT_EQ(map.layers[0].zOffset, 0.0f);
    EXPECT_EQ(map.layers[1].name, "Decor");
    EXPECT_FLOAT_EQ(map.layers[1].zOffset, 0.03f);
    EXPECT_EQ(map.layers[2].name, "Overlay");
    EXPECT_FLOAT_EQ(map.layers[2].zOffset, 0.2f);
    for (const TilemapLayer& layer : map.layers) {
        EXPECT_EQ(static_cast<int32_t>(layer.cells.size()), map.width * map.height);
    }
    // Ground is solid grass everywhere.
    EXPECT_EQ(map.cellAt(0, 0, 0), 1);
    EXPECT_EQ(map.cellAt(31, 19, 0), 1);
    EXPECT_EQ(map.cellAt(32, 19, 0), -1);

    // Trees live in the map now: the canopy sits on the overlay so the player
    // walks behind it, and both canopy (4) and trunk (16) are tileset solids.
    const nlohmann::json& tilesetJson = nlohmann::json::parse(
        readRepoFile("Example/2DRpgPrototype/Content/Tilesets/town.yatileset.json"));
    std::vector<int32_t> solidTiles;
    for (const auto& entry : tilesetJson["solid"]) {
        solidTiles.push_back(entry.get<int32_t>() + 1); // cell value is tile + 1
    }
    int32_t canopyCells = 0;
    int32_t solidCells  = 0;
    for (const TilemapLayer& layer : map.layers) {
        for (const int32_t value : layer.cells) {
            if (value == 0) {
                continue;
            }
            if (std::ranges::find(solidTiles, value) != solidTiles.end()) {
                ++solidCells;
            }
            if (value == 5) { // tile 4, the tree canopy
                ++canopyCells;
            }
        }
    }
    EXPECT_GT(solidCells, 0);
    EXPECT_EQ(canopyCells, 5); // five trees in the scene

    // The player starts on the cell the authored position names: tilemap
    // origin (-16, -10) makes world (0.5, 0.75) cell (16, 10) once the
    // sprite's foot lift is removed.
    const auto& playerPos = (*playerJson)["components"]["TransformComponent"]["_position"];
    const int32_t cellX   = static_cast<int32_t>(playerPos[0].get<float>() + 16.0f);
    const int32_t cellY   = static_cast<int32_t>(playerPos[1].get<float>() + 10.0f);
    EXPECT_EQ(cellX, 16);
    EXPECT_EQ(cellY, 10);
    EXPECT_EQ(map.cellAt(cellX, cellY, 0), 1);
    EXPECT_EQ(map.cellAt(cellX, cellY, 1), 0); // nothing blocking the spawn
    EXPECT_EQ(map.cellAt(cellX, cellY, 2), 0);
}

// The tileset document is well-formed with the atlas and geometry R1a
// needs. Full parseTilesetJson coverage (which deserializes the atlas
// TextureSlot and therefore resolves textures) belongs to the runtime
// smoke for the same App-context reason as above.
TEST(TilemapSceneTest, TownTilesetDocumentIsWellFormed)
{
    const std::string tilesetText =
        readRepoFile("Example/2DRpgPrototype/Content/Tilesets/town.yatileset.json");
    ASSERT_FALSE(tilesetText.empty());
    const nlohmann::json tilesetJson = nlohmann::json::parse(tilesetText);

    EXPECT_EQ(tilesetJson["atlas"]["textureRef"]["__base__"]["AssetRefBase"]["_path"],
              "Content:Textures/tiny_town.png");
    EXPECT_EQ(tilesetJson["tileWidth"], 16);
    EXPECT_EQ(tilesetJson["tileHeight"], 16);
    EXPECT_EQ(tilesetJson["columns"], 12);
    EXPECT_EQ(tilesetJson["margin"], 0);
    EXPECT_EQ(tilesetJson["spacing"], 0);
    ASSERT_TRUE(tilesetJson["solid"].is_array());

    Tileset tileset;
    for (const auto& entry : tilesetJson["solid"]) {
        tileset.solidTiles.push_back(entry.get<int32_t>());
    }
    std::sort(tileset.solidTiles.begin(), tileset.solidTiles.end());
    EXPECT_TRUE(tileset.isSolidTile(4));
    EXPECT_TRUE(tileset.isSolidTile(16));
    // The fence run is an obstacle too, so the map layer that carries it
    // blocks the player through the same list.
    EXPECT_TRUE(tileset.isSolidTile(80));
    EXPECT_TRUE(tileset.isSolidTile(81));
    EXPECT_TRUE(tileset.isSolidTile(82));
    EXPECT_FALSE(tileset.isSolidTile(0));
}

} // namespace ya
