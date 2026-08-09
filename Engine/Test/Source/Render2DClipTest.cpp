// Clip-stack regression guards (Phase 0 of ui-widget-tree-refactor): the
// nested-clip intersection math used by Render2D::pushClipRect is extracted
// into a pure helper so widget clip hierarchy semantics are testable without
// a render session.

#include "GUI/Draw2D/Render2D.h"

#include <gtest/gtest.h>

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
    expectRectEq(Render2D::intersectClipRect(child, parent), child);
}

TEST(Render2DClipTest, ChildLargerThanParentIsClamped)
{
    const Rect2D parent{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    const Rect2D child{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D expected{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    expectRectEq(Render2D::intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, PartialOverlapIntersects)
{
    const Rect2D parent{.pos = {50.0f, 50.0f}, .extent = {200.0f, 200.0f}};
    const Rect2D child{.pos = {150.0f, 100.0f}, .extent = {200.0f, 100.0f}};
    const Rect2D expected{.pos = {150.0f, 100.0f}, .extent = {100.0f, 100.0f}};
    expectRectEq(Render2D::intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, DisjointClipIsEmpty)
{
    const Rect2D parent{.pos = {0.0f, 0.0f}, .extent = {100.0f, 100.0f}};
    const Rect2D child{.pos = {200.0f, 200.0f}, .extent = {50.0f, 50.0f}};
    const Rect2D clipped = Render2D::intersectClipRect(child, parent);
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
    expectRectEq(Render2D::intersectClipRect(child, parent), expected);
}

TEST(Render2DClipTest, NestedClipsChainIdempotently)
{
    const Rect2D outer{.pos = {0.0f, 0.0f}, .extent = {800.0f, 600.0f}};
    const Rect2D middle{.pos = {100.0f, 100.0f}, .extent = {400.0f, 300.0f}};
    const Rect2D inner{.pos = {200.0f, 150.0f}, .extent = {500.0f, 200.0f}};
    const Rect2D expected{.pos = {200.0f, 150.0f}, .extent = {300.0f, 200.0f}};
    // Intersecting step-by-step must equal the direct intersection.
    expectRectEq(Render2D::intersectClipRect(inner, Render2D::intersectClipRect(middle, outer)), expected);
    expectRectEq(Render2D::intersectClipRect(Render2D::intersectClipRect(inner, middle), outer), expected);
}

} // namespace ya
