#include "GameEditor/EditorLayer.h"

#include <functional>
#include <gtest/gtest.h>

namespace ya
{

TEST(EditorSceneDirtyGuardTest, CleanSceneProceedsWithoutAsking)
{
    EditorLayer layer(nullptr);
    bool asked = false;
    bool proceeded = false;
    layer.setUnsavedGuard([&](std::function<void()> proceed) {
        asked = true;
        proceed();
    });

    layer.runAfterUnsavedResolved([&]() { proceeded = true; });
    EXPECT_FALSE(asked);
    EXPECT_TRUE(proceeded);
    EXPECT_FALSE(layer.isSceneDirty());
}

TEST(EditorSceneDirtyGuardTest, DirtySceneAsksGuardThenProceeds)
{
    EditorLayer layer(nullptr);
    bool asked = false;
    bool proceeded = false;
    layer.setUnsavedGuard([&](std::function<void()> proceed) {
        asked = true;
        layer.clearSceneDirty();
        proceed();
    });
    layer.markSceneDirty();
    EXPECT_TRUE(layer.isSceneDirty());

    layer.runAfterUnsavedResolved([&]() { proceeded = true; });
    EXPECT_TRUE(asked);
    EXPECT_TRUE(proceeded);
    EXPECT_FALSE(layer.isSceneDirty());
}

TEST(EditorSceneDirtyGuardTest, CancelLeavesProceedUncalled)
{
    EditorLayer layer(nullptr);
    bool asked = false;
    bool proceeded = false;
    layer.setUnsavedGuard([&](std::function<void()>) { asked = true; });
    layer.markSceneDirty();

    layer.runAfterUnsavedResolved([&]() { proceeded = true; });
    EXPECT_TRUE(asked);
    EXPECT_FALSE(proceeded);
    EXPECT_TRUE(layer.isSceneDirty());
}

TEST(EditorSceneDirtyGuardTest, MissingGuardDoesNotBlockDirtyProceed)
{
    EditorLayer layer(nullptr);
    bool proceeded = false;
    layer.markSceneDirty();
    layer.runAfterUnsavedResolved([&]() { proceeded = true; });
    EXPECT_TRUE(proceeded);
}

} // namespace ya
