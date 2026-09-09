#include "GameEditor/UI/EditorSurfaceContext.h"

#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorSurfaceContextTest, MetricsUseFramebufferOverLogicalWhenBothPositive)
{
    const EditorWindowMetrics metrics = makeEditorWindowMetrics(100, 50, 1.25f, Extent2D{.width = 200, .height = 100});
    EXPECT_EQ(metrics.logicalExtent.width, 100u);
    EXPECT_EQ(metrics.logicalExtent.height, 50u);
    EXPECT_EQ(metrics.framebufferExtent.width, 200u);
    EXPECT_FLOAT_EQ(metrics.dpiScale, 2.0f);
}

TEST(EditorSurfaceContextTest, MetricsKeepWindowDpiWhenFramebufferMissing)
{
    const EditorWindowMetrics metrics = makeEditorWindowMetrics(1280, 720, 1.5f, Extent2D{});
    EXPECT_EQ(metrics.logicalExtent.width, 1280u);
    EXPECT_EQ(metrics.logicalExtent.height, 720u);
    EXPECT_FLOAT_EQ(metrics.dpiScale, 1.5f);
}

TEST(EditorSurfaceContextTest, MetricsClampZeroWindowToOneAndDefaultDpi)
{
    const EditorWindowMetrics metrics = makeEditorWindowMetrics(0, 0, 0.0f, Extent2D{});
    EXPECT_EQ(metrics.logicalExtent.width, 1u);
    EXPECT_EQ(metrics.logicalExtent.height, 1u);
    EXPECT_FLOAT_EQ(metrics.dpiScale, 1.0f);
}

TEST(EditorSurfaceContextTest, ApplyWritesTreeExtentAndDpiWithoutRender)
{
    WidgetTree tree({.width = 16, .height = 16});
    tree.setDpiScale(1.0f);

    const EditorWindowMetrics metrics = makeEditorWindowMetrics(400, 300, 1.0f, Extent2D{.width = 800, .height = 600});
    applyEditorWindowMetrics(tree, metrics);

    EXPECT_EQ(tree.getLogicalExtent().width, 400u);
    EXPECT_EQ(tree.getLogicalExtent().height, 300u);
    EXPECT_FLOAT_EQ(tree.getDpiScale(), 2.0f);
}

} // namespace ya
