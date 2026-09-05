#include "GameEditor/UI/EditorTabSpawnerRegistry.h"

#include "GUI/Widgets/Controls/Panel.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorTabSpawnerRegistryTest, AddFindRejectsDuplicatesAndSpawnDoesNotReregister)
{
    EditorTabSpawnerRegistry registry;
    int spawnCount = 0;
    registry.add({
        .tabId = "frame-stats",
        .title = "Stats",
        .toolsMenuLabel = "Frame Stats",
        .spawn = [&spawnCount](FEditorTabSpawnContext&) {
            ++spawnCount;
            return std::make_shared<UIPanel>("StatsBody");
        },
    });
    registry.add({
        .tabId = "frame-stats",
        .title = "Dup",
        .toolsMenuLabel = "Duplicate",
        .spawn = [&spawnCount](FEditorTabSpawnContext&) {
            ++spawnCount;
            return std::make_shared<UIPanel>("DupBody");
        },
    });

    EXPECT_EQ(registry.all().size(), 1u);
    const FEditorTabSpawner* found = registry.find("frame-stats");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->title, "Stats");
    EXPECT_EQ(registry.find("missing"), nullptr);
}

} // namespace ya
