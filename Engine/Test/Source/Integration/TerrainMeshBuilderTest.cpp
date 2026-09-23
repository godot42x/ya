#include "Render3D/Terrain/TerrainMeshBuilder.h"

#include <gtest/gtest.h>

#include <vector>

using namespace ya;

TEST(TerrainMeshBuilderTest, BuildsGridWithExpectedCountsAndBounds)
{
    const std::vector<float> heights = {
        0.0f, 0.5f,
        1.0f, 0.25f,
    };

    const EngineMeshData mesh = buildTerrainMeshData(TerrainMeshBuildDesc{
        .name           = "test_terrain",
        .size           = glm::vec2(10.0f, 20.0f),
        .heightScale    = 4.0f,
        .heightOffset   = -1.0f,
        .gridResolution = 2,
        .heightWidth    = 2,
        .heightHeight   = 2,
        .heights        = heights,
    });

    ASSERT_EQ(mesh.vertices.size(), 4u);
    ASSERT_EQ(mesh.indices.size(), 6u);

    EXPECT_FLOAT_EQ(mesh.vertices[0].position.x, -5.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].position.z, -10.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[0].position.y, -1.0f);

    EXPECT_FLOAT_EQ(mesh.vertices[3].position.x, 5.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[3].position.z, 10.0f);
    EXPECT_FLOAT_EQ(mesh.vertices[3].position.y, 0.0f);
}

TEST(TerrainMeshBuilderTest, ProducesNormalizedNonZeroNormals)
{
    const std::vector<float> heights = {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
    };

    const EngineMeshData mesh = buildTerrainMeshData(TerrainMeshBuildDesc{
        .size           = glm::vec2(8.0f, 8.0f),
        .heightScale    = 2.0f,
        .gridResolution = 3,
        .heightWidth    = 3,
        .heightHeight   = 3,
        .heights        = heights,
    });

    ASSERT_EQ(mesh.vertices.size(), 9u);
    for (const auto& vertex : mesh.vertices) {
        EXPECT_GT(glm::dot(vertex.normal, vertex.normal), 0.0f);
        EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-4f);
    }
}

TEST(TerrainMeshBuilderTest, ClampsResolution)
{
    EXPECT_EQ(clampTerrainGridResolution(0), 2u);
    EXPECT_EQ(clampTerrainGridResolution(1), 2u);
    EXPECT_EQ(clampTerrainGridResolution(1025), 1024u);
}
