
#pragma once

#include "Core/Base.h"

#include <glm/glm.hpp>


namespace ya
{

struct Vertex
{
    glm::vec3 position;
    glm::vec2 texCoord0;
    glm::vec3 normal;
};

struct GeometryUtils
{

    static void makeCube(
        float leftPlane, float rightPlane, float bottomPlane, float topPlane, float nearPlane, float farPlane,
        std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
        bool bGenTexcoords = false,
        bool bGenNormals = false, glm::mat4 transform = glm::mat4(1.0f));
};


namespace geo
{

struct Vertex
{
    glm::vec3 postion;
};

struct Edge
{
    Vertex start;
    Vertex end;
};

struct Face
{
    Edge edge1;
    Edge edge2;
    Edge edge3;
};

struct Plane
{
    glm::vec3 normal;
    float     d;

    Plane(const glm::vec3 &normal, float d) : normal(normal), d(d) {}
    [[nodiscard]] float distanceTo(const glm::vec3 &point) const
    {
        return glm::dot(normal, point) + d;
    }
};

} // namespace geo

}; // namespace ya