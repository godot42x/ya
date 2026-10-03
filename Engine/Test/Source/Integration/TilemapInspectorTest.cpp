#include "GameEditor/Inspector/PropertyGraph.h"
#include "GameEditor/UI/Sections/EditorAutoPropertySection.h"
#include "Scene2D/TilemapComponent.h"
#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

// The Inspector must offer the map fields R1b promises as editable: grid
// size, cell size, tileset path and the layer list. TilesetRef is not on
// the asset-picker type list, so its path edits as a plain string row.
TEST(TilemapInspectorTest, GraphExposesEditableMapFields)
{
    TilemapComponent map;
    map.width       = 6;
    map.height      = 5;
    map.cellSize    = glm::vec2(1.0f, 1.0f);
    map._editWidth  = 6;
    map._editHeight = 5;
    map.tileset.setPath("Content:Tilesets/town.yatileset.json");
    map.layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(30, 0)});

    auto graph = PropertyGraph::project(type_index_v<TilemapComponent>, {&map});
    EXPECT_NE(graph.find("width"), nullptr);
    EXPECT_NE(graph.find("height"), nullptr);
    EXPECT_NE(graph.find("cellSize"), nullptr);
    // TilesetRef is an asset ref: one asset row (path field + Browse),
    // not a struct subtree. The row edits through AssetRefBase.
    const PropertyNode* tilesetNode = graph.find("tileset");
    ASSERT_NE(tilesetNode, nullptr);
    EXPECT_TRUE(tilesetNode->binding.isAssetRef());
    EXPECT_NE(graph.find("layers"), nullptr);

    auto section = std::make_shared<EditorAutoPropertySection>("AutoTilemap", std::move(graph));
    WidgetTree tree({.width = 360, .height = 400});
    ASSERT_TRUE(tree.attach(*tree.getLayer(WidgetTree::ELayer::Content), section).valid());
    EXPECT_GT(section->getChildren().size(), 0u);
    tree.detach(*section);
}

} // namespace ya

