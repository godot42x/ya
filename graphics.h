#pragma once

#include <glm/glm.hpp>

struct Viewport
{
    uint32_t  width     = 0;
    uint32_t  height    = 0;
    glm::mat4 transform = glm::mat4(1.0f);

    Viewport() = default;
    Viewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
        : width(x), height(y)
    {
        transform = glm::mat4{
            {width / 2, 0, 0, 0},
            {0, height / 2, 0, 0},
            {0, 0, 1, 0},
            {width / 2, height / 2, 0, 1},
        };
    }
};

struct BoundingBox
{
    glm::vec2 min;
    glm::vec2 max;

    BoundingBox() = default;
    BoundingBox(const glm::vec2 &min, const glm::vec2 &max) : min(min), max(max) {}
};

BoundingBox CalcBoundingBox(glm::vec4 *triangle_vertices, const Viewport &viewport)
{

    glm::vec2 min{0, 0};
    glm::vec2 max{viewport.width - 1, viewport.height - 1};

    BoundingBox bbox{max, min};

    for (size_t i = 0; i < 3; ++i)
    {
        bbox.min.x = glm::min(bbox.min.x, triangle_vertices[i].x);
        bbox.min.y = glm::min(bbox.min.y, triangle_vertices[i].y);
        bbox.max.x = glm::max(bbox.max.x, triangle_vertices[i].x);
        bbox.max.y = glm::max(bbox.max.y, triangle_vertices[i].y);
    }

    bbox.min = glm::max(min, bbox.min);
    bbox.max = glm::min(max, bbox.max);
    return bbox;
}

glm::vec3 BaryCentric(glm::vec4 *triangle_vertices, const glm::vec2 &p)
{
    glm::vec2 v0{triangle_vertices[0]};
    glm::vec2 v1{triangle_vertices[1]};
    glm::vec2 v2{triangle_vertices[2]};

    glm::vec2 v01 = v1 - v0;
    glm::vec2 v02 = v2 - v0;
    glm::vec2 vp0 = v0 - p;

    glm::vec3 vmz = glm::cross(glm::vec3(v01.x, v02.x, vp0.x),
                               glm::vec3(v01.y, v02.y, vp0.y));
    if (vmz.z < 1e-2) {
        return {-1, -1, -1};
    }

    float v = vmz.x / vmz.z;
    float w = vmz.y / vmz.z;
    float u = 1.0f - (v + w);
    return {u, v, w};
}