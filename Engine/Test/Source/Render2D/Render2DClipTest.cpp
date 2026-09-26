// Clip-stack regression guards (Phase 0 of ui-widget-tree-refactor): the
// nested-clip intersection math used by the 2D clip stack is extracted into a
// pure helper so widget clip hierarchy semantics are testable without a
// render session. The Render2DList tests cover the builder's value semantics
// (command boundaries, clip snapshots) with no GPU device.

#include "Render2D/Render2D.h"
#include "Render2D/Render2DList.h"

#include <gtest/gtest.h>

#include <utility>

namespace ya
{

namespace
{

void expectRectEq(const Rect2D& actual, const Rect2D& expected)
{
    EXPECT_EQ(actual.pos, expected.pos);
    EXPECT_EQ(actual.extent, expected.extent);
}

} // namespace

TEST(Render2DClipTest, ChildInsideParentKeepsRect)
{
    const Rect2D parent{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D child{.pos = {100.0f, 100.0f}, .extent = {200.0f, 80.0f}};
    expectRectEq(intersectClipRect(child, parent), child);
}

TEST(Render2DClipTest, ChildLargerThanParentIsClamped)
{
    const Rect2D parent{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    const Rect2D child{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D expected{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    expectRectEq(intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, PartialOverlapIntersects)
{
    const Rect2D parent{.pos = {50.0f, 50.0f}, .extent = {200.0f, 200.0f}};
    const Rect2D child{.pos = {150.0f, 100.0f}, .extent = {200.0f, 100.0f}};
    const Rect2D expected{.pos = {150.0f, 100.0f}, .extent = {100.0f, 100.0f}};
    expectRectEq(intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, DisjointClipIsEmpty)
{
    const Rect2D parent{.pos = {0.0f, 0.0f}, .extent = {100.0f, 100.0f}};
    const Rect2D child{.pos = {200.0f, 200.0f}, .extent = {50.0f, 50.0f}};
    const Rect2D clipped = intersectClipRect(child, parent);
    EXPECT_EQ(clipped.pos, glm::vec2(200.0f, 200.0f));
    EXPECT_EQ(clipped.extent, glm::vec2(0.0f, 0.0f));
}

TEST(Render2DClipTest, EdgeTouchingClipIsZeroWidthSliver)
{
    const Rect2D parent{.pos = {0.0f, 0.0f}, .extent = {100.0f, 100.0f}};
    const Rect2D child{.pos = {100.0f, 0.0f}, .extent = {50.0f, 50.0f}};
    // The rect touches the parent edge: the overlap is a zero-width sliver.
    // A zero-width scissor clips everything, matching the disjoint case.
    const Rect2D expected{.pos = {100.0f, 0.0f}, .extent = {0.0f, 50.0f}};
    expectRectEq(intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, NestedClipsChainIdempotently)
{
    const Rect2D outer{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D middle{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    const Rect2D inner{.pos = {200.0f, 150.0f}, .extent = {500.0f, 200.0f}};
    const Rect2D expected{.pos = {200.0f, 150.0f}, .extent = {300.0f, 200.0f}};
    // Intersecting step-by-step must equal the direct intersection.
    expectRectEq(intersectClipRect(inner, intersectClipRect(middle, outer)), expected);
    expectRectEq(intersectClipRect(intersectClipRect(inner, middle), outer), expected);
}

TEST(Render2DPassSlotTest, AcquireReturnsDistinctSlotsAndReleaseRecycles)
{
    const Render2DPassSlot a = Render2D::acquirePassSlot();
    const Render2DPassSlot b = Render2D::acquirePassSlot();
    EXPECT_NE(a, b);
    EXPECT_NE(a, kInvalidRender2DPassSlot);
    EXPECT_NE(b, kInvalidRender2DPassSlot);

    Render2D::releasePassSlot(a);
    const Render2DPassSlot recycled = Render2D::acquirePassSlot();
    EXPECT_EQ(recycled, a);
    Render2D::releasePassSlot(b);
    Render2D::releasePassSlot(recycled);
}

TEST(Render2DListTest, SameInputBuildsSameList)
{
    Render2DList a;
    Render2DList b;
    const auto build = [](Render2DList& list)
    {
        list.pushClipRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {1280.0f, 720.0f}});
        list.makeSprite(glm::vec3(10.0f, 10.0f, 0.0f), glm::vec2(64.0f, 32.0f));
        list.makeWireBox(glm::mat4(1.0f), glm::vec3(1.0f), glm::vec4(1.0f));
        list.popClipRect();
    };
    build(a);
    build(b);
    ASSERT_EQ(a.commands.size(), b.commands.size());
    ASSERT_EQ(a.screenVerts.size(), b.screenVerts.size());
    ASSERT_EQ(a.worldVerts.size(), b.worldVerts.size());
    ASSERT_EQ(a.lineVerts.size(), b.lineVerts.size());
    for (size_t i = 0; i < a.commands.size(); ++i) {
        EXPECT_EQ(a.commands[i].kind, b.commands[i].kind);
        EXPECT_EQ(a.commands[i].firstVertex, b.commands[i].firstVertex);
        EXPECT_EQ(a.commands[i].vertexCount, b.commands[i].vertexCount);
    }
}

TEST(Render2DListTest, ClipChangeClosesCommandWithOldClip)
{
    Render2DList list;
    list.pushClipRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}});
    list.makeSprite(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec2(10.0f, 10.0f));
    list.pushClipRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {400.0f, 300.0f}});
    // The clip change closes the pending command, and the closed command
    // carries the clip that was in effect when its geometry was emitted.
    ASSERT_EQ(list.commands.size(), 1u);
    EXPECT_TRUE(list.commands[0].bClipped);
    EXPECT_EQ(list.commands[0].clip.extent.x, 800.0f);
}

TEST(Render2DListTest, KindChangeSplitsCommands)
{
    Render2DList list;
    list.makeSprite(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec2(10.0f, 10.0f));
    list.makeWireBox(glm::mat4(1.0f), glm::vec3(1.0f), glm::vec4(1.0f));
    list.pushClipRect(Rect2D{.pos = {0.0f, 0.0f}, .extent = {100.0f, 100.0f}});
    // The kind change and the clip change each close a command; the pending
    // Line batch is not materialized until its own boundary.
    ASSERT_EQ(list.commands.size(), 2u);
    EXPECT_EQ(list.commands[0].kind, ERender2dBatchKind::ScreenQuad);
    EXPECT_EQ(list.commands[1].kind, ERender2dBatchKind::Line);
    EXPECT_EQ(list.commands[0].bClipped, false);
    EXPECT_EQ(list.lineVerts.size(), 24u);
}

} // namespace ya
