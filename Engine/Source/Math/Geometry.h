
#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>


namespace ya
{

struct Vertex
{
    glm::vec3 position;
    glm::vec2 texCoord0;
    glm::vec4 normal;
};

struct GeometryUtils
{

    static void createCube(
        float leftPlane, float rightPlane, float bottomPlane, float topPlane, float nearPlane, float farPlane,
        std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
        bool bUseTexcoords = false, bool useNormals = false);
};
}; // namespace ya