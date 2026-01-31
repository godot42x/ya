#include "Geometry.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace ya
{



void PrimitiveGeometry::createCube(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    createCube(glm::vec3(1.0f), outVertices, outIndices);
}

void PrimitiveGeometry::createCube(const glm::vec3 &size, std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    float hw = size.x * 0.5f; // half width
    float hh = size.y * 0.5f; // half height
    float hd = size.z * 0.5f; // half depth

    // Right-Handed coordinate system (OpenGL/Vulkan/Blender convention):
    // X+ right, Y+ up, Z+ toward viewer (out of screen)
    // Counter-clockwise winding when viewed from outside
    //
    // 24 vertices (4 per face) - needed for proper normals and UVs
    outVertices = {
        // Front face (Z-)
        {.position = {-hw, -hh, -hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}},
        {.position = {hw, -hh, -hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, -1.0f}},
        {.position = {hw, hh, -hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}},
        {.position = {-hw, hh, -hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, -1.0f}},

        // Back face (Z+)
        {.position = {hw, -hh, hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {-hw, -hh, hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {-hw, hh, hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {hw, hh, hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},

        // Left face (X-)
        {.position = {-hw, -hh, hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {-1.0f, 0.0f, 0.0f}},
        {.position = {-hw, -hh, -hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {-1.0f, 0.0f, 0.0f}},
        {.position = {-hw, hh, -hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {-1.0f, 0.0f, 0.0f}},
        {.position = {-hw, hh, hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {-1.0f, 0.0f, 0.0f}},

        // Right face (X+)
        {.position = {hw, -hh, -hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {1.0f, 0.0f, 0.0f}},
        {.position = {hw, -hh, hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {1.0f, 0.0f, 0.0f}},
        {.position = {hw, hh, hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}},
        {.position = {hw, hh, -hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {1.0f, 0.0f, 0.0f}},

        // Bottom face (Y-)
        {.position = {-hw, -hh, hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {0.0f, -1.0f, 0.0f}},
        {.position = {hw, -hh, hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {0.0f, -1.0f, 0.0f}},
        {.position = {hw, -hh, -hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}},
        {.position = {-hw, -hh, -hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {0.0f, -1.0f, 0.0f}},

        // Top face (Y+)
        {.position = {-hw, hh, -hd}, .texCoord0 = {0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = {hw, hh, -hd}, .texCoord0 = {1.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = {hw, hh, hd}, .texCoord0 = {1.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}},
        {.position = {-hw, hh, hd}, .texCoord0 = {0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}}};

    // clang-format off
    // 36 indices (6 faces * 2 triangles * 3 vertices)
    outIndices = {
        0,  1,  2,  2,  3,  0,   // Front
        4,  5,  6,  6,  7,  4,   // Back
        8,  9,  10, 10, 11, 8,   // Left
        12, 13, 14, 14, 15, 12,  // Right
        16, 17, 18, 18, 19, 16,  // Bottom
        20, 21, 22, 22, 23, 20   // Top
    };
    // clang-format on
}

void PrimitiveGeometry::createSphere(float radius, uint32_t slices, uint32_t stacks,
                                     std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    outVertices.clear();
    outIndices.clear();

    // Generate vertices
    for (uint32_t stack = 0; stack <= stacks; ++stack) {
        float phi = glm::pi<float>() * float(stack) / float(stacks); // 0 to PI
        float y   = radius * cos(phi);
        float r   = radius * sin(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * glm::pi<float>() * float(slice) / float(slices); // 0 to 2PI
            float x     = r * cos(theta);
            float z     = r * sin(theta);

            Vertex v;
            v.position  = {x, y, z};
            v.normal    = glm::normalize(v.position);
            v.texCoord0 = {
                float(slice) / float(slices),
                float(stack) / float(stacks)};
            outVertices.push_back(v);
        }
    }

    // Generate indices
    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            uint32_t first  = stack * (slices + 1) + slice;
            uint32_t second = first + slices + 1;

            // First triangle
            outIndices.push_back(first);
            outIndices.push_back(second);
            outIndices.push_back(first + 1);

            // Second triangle
            outIndices.push_back(second);
            outIndices.push_back(second + 1);
            outIndices.push_back(first + 1);
        }
    }
}

void PrimitiveGeometry::createPlane(float width, float depth, float uRepeat, float vRepeat,
                                    std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    float hw = width * 0.5f;
    float hd = depth * 0.5f;

    outVertices = {
        {{-hw, 0.0f, -hd}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{hw, 0.0f, -hd}, {uRepeat, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{hw, 0.0f, hd}, {uRepeat, vRepeat}, {0.0f, 1.0f, 0.0f}},
        {{-hw, 0.0f, hd}, {0.0f, vRepeat}, {0.0f, 1.0f, 0.0f}}};

    outIndices = {0, 1, 2, 2, 3, 0};
}

void PrimitiveGeometry::createCylinder(float radius, float height, uint32_t segments,
                                       std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    outVertices.clear();
    outIndices.clear();

    float halfHeight = height * 0.5f;

    // Side vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float     theta  = 2.0f * glm::pi<float>() * float(i) / float(segments);
        float     x      = radius * cos(theta);
        float     z      = radius * sin(theta);
        glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));

        // Bottom vertex
        outVertices.push_back({
            .position  = {x, -halfHeight, z},
            .texCoord0 = {float(i) / segments, 0.0f},
            .normal    = normal,
        });
        // Top vertex
        outVertices.push_back({
            .position  = {x, halfHeight, z},
            .texCoord0 = {float(i) / segments, 1.0f},
            .normal    = normal,
        });
    }

    // Side indices
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t base = i * 2;
        outIndices.push_back(base);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 1);

        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);
    }

    // Caps (simplified - center vertex + ring)
    uint32_t bottomCenterIdx = outVertices.size();
    outVertices.push_back({
        .position  = {0.0f, -halfHeight, 0.0f},
        .texCoord0 = {0.5f, 0.5f},
        .normal    = {0.0f, -1.0f, 0.0f},
    });

    uint32_t topCenterIdx = outVertices.size();
    outVertices.push_back({
        .position  = {0.0f, halfHeight, 0.0f},
        .texCoord0 = {0.5f, 0.5f},
        .normal    = {0.0f, 1.0f, 0.0f},
    });

    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = 2.0f * glm::pi<float>() * float(i) / float(segments);
        float x     = radius * cos(theta);
        float z     = radius * sin(theta);

        outVertices.push_back({
            .position  = {x, -halfHeight, z},
            .texCoord0 = {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)},
            .normal    = {0.0f, -1.0f, 0.0f},
        });
        outVertices.push_back({
            .position  = {x, halfHeight, z},
            .texCoord0 = {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)},
            .normal    = {0.0f, 1.0f, 0.0f},
        });
    }

    uint32_t capStartIdx = bottomCenterIdx + 2;
    for (uint32_t i = 0; i < segments; ++i) {
        // Bottom cap
        outIndices.push_back(bottomCenterIdx);
        outIndices.push_back(capStartIdx + i * 2);
        outIndices.push_back(capStartIdx + (i + 1) * 2);

        // Top cap
        outIndices.push_back(topCenterIdx);
        outIndices.push_back(capStartIdx + (i + 1) * 2 + 1);
        outIndices.push_back(capStartIdx + i * 2 + 1);
    }
}

void PrimitiveGeometry::createCone(float radius, float height, uint32_t segments,
                                   std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    outVertices.clear();
    outIndices.clear();

    // Apex
    outVertices.push_back({{0.0f, height, 0.0f}, {0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}});

    // Base center
    uint32_t baseCenterIdx = outVertices.size();
    outVertices.push_back({{0.0f, 0.0f, 0.0f}, {0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}});

    // Base ring vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = 2.0f * glm::pi<float>() * float(i) / float(segments);
        float x     = radius * cos(theta);
        float z     = radius * sin(theta);

        // Side vertex (for cone surface)
        glm::vec3 toApex  = glm::normalize(glm::vec3(0.0f, height, 0.0f) - glm::vec3(x, 0.0f, z));
        glm::vec3 tangent = glm::normalize(glm::vec3(-z, 0.0f, x));
        glm::vec3 normal  = glm::normalize(glm::cross(tangent, toApex));

        outVertices.push_back({{x, 0.0f, z}, {float(i) / segments, 1.0f}, normal});

        // Base vertex (for bottom cap)
        outVertices.push_back({{x, 0.0f, z}, {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}, {0.0f, -1.0f, 0.0f}});
    }

    // Side triangles
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t current = baseCenterIdx + 1 + i * 2;
        uint32_t next    = baseCenterIdx + 1 + (i + 1) * 2;

        outIndices.push_back(0); // Apex
        outIndices.push_back(current);
        outIndices.push_back(next);
    }

    // Base triangles
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t current = baseCenterIdx + 2 + i * 2;
        uint32_t next    = baseCenterIdx + 2 + (i + 1) * 2;

        outIndices.push_back(baseCenterIdx);
        outIndices.push_back(current);
        outIndices.push_back(next);
    }
}

void PrimitiveGeometry::createFullscreenQuad(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    // NDC coordinates, suitable for post-processing
    outVertices = {
        {.position = {-1.0f, -1.0f, 0.0f}, .texCoord0 = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {1.0f, -1.0f, 0.0f}, .texCoord0 = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {1.0f, 1.0f, 0.0f}, .texCoord0 = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}},
        {.position = {-1.0f, 1.0f, 0.0f}, .texCoord0 = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}}};

    outIndices = {0, 1, 2, 2, 3, 0};
    outIndices.insert(outIndices.end(), {0, 3, 2, 0, 2, 1}); // back face
}



} // namespace ya