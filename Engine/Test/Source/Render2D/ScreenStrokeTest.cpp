#include "Render2D/ScreenDrawList.h"

#include <gtest/gtest.h>

#include <cmath>

namespace ya
{

namespace
{

void seal(ScreenDrawList& list)
{
    list.seal();
}

glm::vec2 vertexXY(const ScreenVertex& vertex)
{
    return {vertex.pos.x, vertex.pos.y};
}

} // namespace

TEST(ScreenStrokeTest, QuadsKeepTheTwoTriangleIndexPattern)
{
    ScreenDrawList list;
    list.makeSprite(glm::vec3(1.0f, 2.0f, 0.0f), glm::vec2(10.0f, 8.0f));
    seal(list);

    ASSERT_EQ(list.commands.size(), 1u);
    EXPECT_EQ(list.commands[0].firstIndex, 0u);
    EXPECT_EQ(list.commands[0].indexCount, 6u);
    ASSERT_EQ(list.indices.size(), 6u);
    const uint32_t expected[6] = {0, 1, 3, 0, 3, 2};
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(list.indices[static_cast<size_t>(i)], expected[i]);
    }
    EXPECT_EQ(list.capturedStats().screenIndexCount, 6u);
}

TEST(ScreenStrokeTest, ClosedPolylineAddsTheClosingSegment)
{
    const glm::vec2 points[] = {
        {0.0f, 0.0f},
        {40.0f, 0.0f},
        {40.0f, 30.0f},
        {0.0f, 30.0f},
    };
    const glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};

    ScreenDrawList open;
    open.strokePolyline(points, false, color, 2.0f);
    seal(open);

    ScreenDrawList closed;
    closed.strokePolyline(points, true, color, 2.0f);
    seal(closed);

    EXPECT_EQ(open.vertices.size(), 12u);
    EXPECT_EQ(open.indices.size(), 18u);
    EXPECT_EQ(closed.vertices.size(), 16u);
    EXPECT_EQ(closed.indices.size(), 24u);

    bool bFoundClosingSide = false;
    for (const ScreenVertex& vertex : closed.vertices) {
        if (vertex.pos.x < 0.0f) {
            bFoundClosingSide = true;
            EXPECT_NEAR(vertex.pos.x, -1.0f, 1e-4f);
            EXPECT_GE(vertex.pos.y, -1e-3f);
            EXPECT_LE(vertex.pos.y, 30.0f + 1e-3f);
        }
    }
    EXPECT_TRUE(bFoundClosingSide);

    for (const ScreenVertex& vertex : open.vertices) {
        EXPECT_GE(vertex.pos.x, -1e-3f);
    }
}

TEST(ScreenStrokeTest, HardStrokeIsCenteredOnTheSegment)
{
    ScreenDrawList list;
    list.strokeLine(glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 0.0f), glm::vec4(1.0f), 4.0f);
    seal(list);

    ASSERT_EQ(list.vertices.size(), 4u);
    for (const ScreenVertex& vertex : list.vertices) {
        EXPECT_TRUE(vertex.pos.y == -2.0f || vertex.pos.y == 2.0f);
        EXPECT_GE(vertex.pos.x, 0.0f);
        EXPECT_LE(vertex.pos.x, 10.0f);
    }
}

TEST(ScreenStrokeTest, DegenerateSegmentIsACenteredSquare)
{
    ScreenDrawList list;
    const glm::vec4 color{0.2f, 0.4f, 0.6f, 1.0f};
    list.strokeLine(glm::vec2(8.0f, 8.0f), glm::vec2(8.0f, 8.0f), color, 6.0f);
    seal(list);

    ASSERT_EQ(list.vertices.size(), 4u);
    glm::vec2 minP{1e9f, 1e9f};
    glm::vec2 maxP{-1e9f, -1e9f};
    for (const ScreenVertex& vertex : list.vertices) {
        minP = glm::min(minP, vertexXY(vertex));
        maxP = glm::max(maxP, vertexXY(vertex));
        EXPECT_FLOAT_EQ(vertex.color.a, 1.0f);
    }
    EXPECT_FLOAT_EQ(minP.x, 5.0f);
    EXPECT_FLOAT_EQ(minP.y, 5.0f);
    EXPECT_FLOAT_EQ(maxP.x, 11.0f);
    EXPECT_FLOAT_EQ(maxP.y, 11.0f);
    const glm::vec2 center = (minP + maxP) * 0.5f;
    EXPECT_FLOAT_EQ(center.x, 8.0f);
    EXPECT_FLOAT_EQ(center.y, 8.0f);
}

TEST(ScreenStrokeTest, FeatherWidthSitsOutsideTheCore)
{
    ScreenDrawList list;
    const glm::vec4 color{1.0f, 1.0f, 1.0f, 0.5f};
    list.strokeLine(glm::vec2(10.0f, 20.0f), glm::vec2(110.0f, 20.0f), color, 4.0f, 2.0f);
    seal(list);

    ASSERT_EQ(list.vertices.size(), 8u);
    EXPECT_EQ(list.indices.size(), 18u);
    EXPECT_NE(list.indices.size(), list.vertices.size() * 6 / 4);

    int opaque = 0;
    int clear = 0;
    for (const ScreenVertex& vertex : list.vertices) {
        EXPECT_TRUE(vertex.pos.x == 10.0f || vertex.pos.x == 110.0f);
        const float distance = std::abs(vertex.pos.y - 20.0f);
        if (vertex.color.a == 0.0f) {
            EXPECT_NEAR(distance, 4.0f, 1e-4f);
            ++clear;
        }
        else {
            EXPECT_FLOAT_EQ(vertex.color.a, 0.5f);
            EXPECT_NEAR(distance, 2.0f, 1e-4f);
            ++opaque;
        }
    }
    EXPECT_EQ(opaque, 4);
    EXPECT_EQ(clear, 4);
}

TEST(ScreenStrokeTest, ArcAndBezierAreSampledPolylines)
{
    ScreenDrawList arc;
    arc.strokeArc(glm::vec2(0.0f), 10.0f, 0.0f, 1.5707963f, 4, glm::vec4(1.0f), 1.0f);
    seal(arc);
    EXPECT_EQ(arc.vertices.size(), 16u);

    ScreenDrawList curve;
    curve.strokeBezierCubic(glm::vec2(0.0f), glm::vec2(0.0f, 10.0f),
                            glm::vec2(10.0f, 10.0f), glm::vec2(10.0f, 0.0f),
                            4, glm::vec4(1.0f), 1.0f);
    seal(curve);
    EXPECT_EQ(curve.vertices.size(), 16u);
    EXPECT_EQ(curve.indices.size(), curve.vertices.size() / 4 * 6);
}

TEST(ScreenStrokeTest, FillConvexPolyIsATriangleFan)
{
    const glm::vec2 points[] = {
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {10.0f, 8.0f},
        {0.0f, 8.0f},
    };
    ScreenDrawList list;
    list.fillConvexPoly(points, glm::vec4(1.0f));
    seal(list);

    ASSERT_EQ(list.vertices.size(), 4u);
    ASSERT_EQ(list.indices.size(), 6u);
    EXPECT_EQ(list.indices[0], 0u);
    EXPECT_EQ(list.indices[1], 1u);
    EXPECT_EQ(list.indices[2], 2u);
    EXPECT_EQ(list.indices[3], 0u);
    EXPECT_EQ(list.indices[4], 2u);
    EXPECT_EQ(list.indices[5], 3u);
}

} // namespace ya
