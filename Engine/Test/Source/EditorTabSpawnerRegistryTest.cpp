#include "GameEditor/UI/EditorTabSpawnerRegistry.h"
#include "GameEditor/UI/EditorViewportHost.h"

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
            return std::make_shared<UICanvasPanel>("StatsBody");
        },
    });
    registry.add({
        .tabId = "frame-stats",
        .title = "Dup",
        .toolsMenuLabel = "Duplicate",
        .spawn = [&spawnCount](FEditorTabSpawnContext&) {
            ++spawnCount;
            return std::make_shared<UICanvasPanel>("DupBody");
        },
    });

    EXPECT_EQ(registry.all().size(), 1u);
    const FEditorTabSpawner* found = registry.find("frame-stats");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->title, "Stats");
    EXPECT_EQ(registry.find("missing"), nullptr);
}

TEST(EditorTabSpawnerRegistryTest, BuiltinRegistryIncludesHierarchyAndViewport)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* hierarchy = registry.find("hierarchy");
    ASSERT_NE(hierarchy, nullptr);
    EXPECT_EQ(hierarchy->title, "Hierarchy");
    EXPECT_EQ(hierarchy->toolsMenuLabel, "Hierarchy");
    EXPECT_TRUE(static_cast<bool>(hierarchy->spawn));

    const FEditorTabSpawner* viewport = registry.find("viewport");
    ASSERT_NE(viewport, nullptr);
    EXPECT_EQ(viewport->title, "Viewport");
    EXPECT_EQ(viewport->toolsMenuLabel, "Viewport");
    EXPECT_TRUE(static_cast<bool>(viewport->spawn));
    EXPECT_EQ(registry.find("gui-workbench"), nullptr);

    const FEditorTabSpawner* level = registry.find("level-editor");
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->title, "Level");
    EXPECT_EQ(level->scope, EEditorTabScope::WindowRootEditor);
    EXPECT_EQ(level->detachPolicy, EEditorTabDetachPolicy::Locked);

    const FEditorTabSpawner* play = registry.find("play-toolbar");
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->title, "Play");
    EXPECT_EQ(play->scope, EEditorTabScope::EditorOwnedTool);
    EXPECT_EQ(play->ownerEditorId, kLevelEditorRootId);
    EXPECT_EQ(play->placement, EEditorTabPlacement::EditorOwnedNested);
    EXPECT_EQ(play->detachPolicy, EEditorTabDetachPolicy::TearOffKeepOwner);
}

TEST(EditorTabSpawnerRegistryTest, BuiltinViewportSpawnRequiresHost)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* viewport = registry.find("viewport");
    ASSERT_NE(viewport, nullptr);

    FEditorTabSpawnContext ctx;
    EXPECT_EQ(viewport->spawn(ctx), nullptr);

    struct FSink final : IEditorViewportHostSink
    {
        void setViewportHost(IEditorViewportHost*) override {}
    } sink;
    ctx.viewportHost = &sink;
    EXPECT_NE(viewport->spawn(ctx), nullptr);
}

TEST(EditorTabSpawnerRegistryTest, BuiltinPlayToolbarSpawnDoesNotRequireHost)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* play = registry.find("play-toolbar");
    ASSERT_NE(play, nullptr);

    FEditorTabSpawnContext ctx;
    EXPECT_NE(play->spawn(ctx), nullptr);
    EXPECT_TRUE(static_cast<bool>(play->onSpawnComplete));
}

TEST(EditorTabSpawnerRegistryTest, BuiltinFontAtlasesSpawnDoesNotRequireHost)
{
    EditorTabSpawnerRegistry registry;
    registerBuiltinEditorTabSpawners(registry);
    const FEditorTabSpawner* fonts = registry.find("font-atlases");
    ASSERT_NE(fonts, nullptr);
    EXPECT_EQ(fonts->title, "Fonts");
    EXPECT_EQ(fonts->toolsMenuLabel, "Font Atlases");
    EXPECT_EQ(fonts->scope, EEditorTabScope::WindowTool);

    FEditorTabSpawnContext ctx;
    EXPECT_NE(fonts->spawn(ctx), nullptr);
}

} // namespace ya
