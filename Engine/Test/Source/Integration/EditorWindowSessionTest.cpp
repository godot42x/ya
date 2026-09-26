#include "GameEditor/UI/Shell/EditorWindowRegistry.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorWindowSessionTest, RegistryFindsOnlyTheDefaultWindow)
{
    EditorWindowRegistry windows;
    EditorWindowSession* found = windows.find(kDefaultEditorWindowId);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, &windows.defaultSession());
    EXPECT_EQ(found->windowId(), kDefaultEditorWindowId);
    EXPECT_EQ(windows.find(kInvalidEditorWindowId), nullptr);
    EXPECT_EQ(windows.find(kDefaultEditorWindowId + 1), nullptr);
}

TEST(EditorWindowSessionTest, RegistryCreatesIsolatedSecondSession)
{
    EditorWindowRegistry windows;
    EXPECT_EQ(windows.create(kInvalidEditorWindowId), nullptr);
    EXPECT_EQ(windows.create(kDefaultEditorWindowId), nullptr);

    constexpr EditorWindowId kExtra = 2;
    EditorWindowSession* extra = windows.create(kExtra);
    ASSERT_NE(extra, nullptr);
    EXPECT_EQ(extra->windowId(), kExtra);
    EXPECT_EQ(windows.find(kExtra), extra);
    EXPECT_NE(extra, &windows.defaultSession());
    EXPECT_EQ(windows.create(kExtra), nullptr);

    windows.defaultSession().activeRoot().selection().select("entity-a");
    extra->activeRoot().selection().select("entity-b");
    EXPECT_TRUE(windows.defaultSession().activeRoot().selection().contains("entity-a"));
    EXPECT_FALSE(windows.defaultSession().activeRoot().selection().contains("entity-b"));
    EXPECT_TRUE(extra->activeRoot().selection().contains("entity-b"));
    EXPECT_FALSE(extra->activeRoot().selection().contains("entity-a"));

    EXPECT_NE(&windows.defaultSession().surface(), &extra->surface());
    EXPECT_NE(windows.defaultSession().surface().windowRootDock(), extra->surface().windowRootDock());
    EXPECT_NE(windows.defaultSession().surface().ownedNestedDock(), extra->surface().ownedNestedDock());
    EXPECT_NE(&windows.defaultSession().surface().viewOverlayHost(),
              &extra->surface().viewOverlayHost());
    EXPECT_EQ(windows.defaultSession().tree(), nullptr);
    EXPECT_EQ(extra->tree(), nullptr);

    EXPECT_FALSE(windows.destroy(kDefaultEditorWindowId));
    EXPECT_TRUE(windows.destroy(kExtra));
    EXPECT_EQ(windows.find(kExtra), nullptr);
    EXPECT_FALSE(windows.destroy(kExtra));
}

TEST(EditorWindowSessionTest, SessionOwnsSurfaceWithoutBuildingChrome)
{
    const EditorWindowSession session;
    EXPECT_EQ(session.windowId(), kDefaultEditorWindowId);
    EXPECT_EQ(session.tree(), nullptr);
    ASSERT_NE(session.surface().windowRootDock(), nullptr);
    ASSERT_NE(session.surface().ownedNestedDock(), nullptr);
    EXPECT_NE(session.surface().windowRootDock(), session.surface().ownedNestedDock());
    EXPECT_FALSE(session.wantsTextInput());
    EXPECT_FALSE(session.isViewportHovered());
}

} // namespace ya
