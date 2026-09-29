#include "GameEditor/UI/Ops/EditorTilemapUndo.h"

#include "ECS/Component/2D/TilemapComponent.h"
#include "GUI/Binding/UndoStack.h"
#include "Scene/Core/Scene.h"

#include <gtest/gtest.h>

namespace ya
{

// One stroke (press to release) is one undo step no matter how many cells
// it touched: the step stores whole-layer before/after cell data.
TEST(TilemapEditUndoTest, StrokeIsOneUndoStep)
{
    Scene scene("Stroke");
    Node* node = scene.createNode3D("Tilemap");
    ASSERT_NE(node, nullptr);
    Entity* entity = node->getEntity();
    ASSERT_NE(entity, nullptr);
    auto* id = entity->getComponent<IDComponent>();
    ASSERT_NE(id, nullptr);
    auto* map = entity->addComponent<TilemapComponent>();
    ASSERT_NE(map, nullptr);
    map->width       = 4;
    map->height      = 4;
    map->cellSize    = glm::vec2(1.0f, 1.0f);
    map->_editWidth  = 4;
    map->_editHeight = 4;
    map->layers.push_back(TilemapLayer{.name = "Ground", .cells = std::vector<int32_t>(16, 0)});

    const uint64_t uuid = id->_id.value;
    FTileLayerSnapshot before{uuid, 0, map->layers[0].cells};

    // What a three-cell paint stroke does to the layer.
    ASSERT_TRUE(map->setCell(1, 1, 0, 2));
    ASSERT_TRUE(map->setCell(2, 1, 0, 2));
    ASSERT_TRUE(map->setCell(2, 2, 0, 2));
    FTileLayerSnapshot after{uuid, 0, map->layers[0].cells};

    UndoStack undo;
    ASSERT_TRUE(pushTileLayerUndo(undo, &scene, std::move(before), std::move(after)));
    EXPECT_EQ(undo.undoCount(), 1u);

    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(map->cellAt(1, 1, 0), 0);
    EXPECT_EQ(map->cellAt(2, 2, 0), 0);
    ASSERT_TRUE(undo.redo());
    EXPECT_EQ(map->cellAt(1, 1, 0), 2);
    EXPECT_EQ(map->cellAt(2, 2, 0), 2);

    // Identical snapshots record nothing.
    FTileLayerSnapshot same{uuid, 0, map->layers[0].cells};
    EXPECT_FALSE(pushTileLayerUndo(undo, &scene, same, same));
    EXPECT_EQ(undo.undoCount(), 1u);
}

} // namespace ya

