#include "Render3D/WorldDraw.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(WorldDrawListTest, LineOrderPreservedWithinTheList)
{
    WorldDrawList list;
    list.makeLine(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec4(1.0f));
    list.makeLine(glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f), glm::vec4(1.0f));
    EXPECT_TRUE(list.commands.empty());
    list.seal();

    ASSERT_EQ(list.commands.size(), 1u);
    ASSERT_EQ(list.vertices.size(), 4u);
    EXPECT_EQ(list.commands[0].vertexCount, 4u);
    EXPECT_EQ(list.vertices[0].pos, glm::vec3(0.0f));
    EXPECT_EQ(list.vertices[1].pos, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(list.vertices[2].pos, glm::vec3(2.0f, 0.0f, 0.0f));
    EXPECT_EQ(list.vertices[3].pos, glm::vec3(3.0f, 0.0f, 0.0f));
}

} // namespace ya
