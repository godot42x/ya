#include "GameEditor/UI/EditorWindowRegistry.h"

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
    EXPECT_EQ(windows.find(0), nullptr);
    EXPECT_EQ(windows.find(kDefaultEditorWindowId + 1), nullptr);
}

TEST(EditorWindowSessionTest, SessionOwnsSurfaceWithoutBuildingChrome)
{
    const EditorWindowSession session;
    EXPECT_EQ(session.windowId(), kDefaultEditorWindowId);
    EXPECT_EQ(session.tree(), nullptr);
    EXPECT_FALSE(session.wantsTextInput());
    EXPECT_FALSE(session.isViewportHovered());
}

} // namespace ya
