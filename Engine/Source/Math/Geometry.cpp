#include "Geometry.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace ya
{

void GeometryUtils::makeCube(
    float leftPlane, float rightPlane, float bottomPlane, float topPlane, float nearPlane, float farPlane,
    std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
    bool bGenTexcoords,
    bool bGenNormals /*, glm::mat4 transform*/)
{

    // to generate each vertex's normal or texcoord, we need not only 8 vertices
    // but 24 vertices (4 vertices per face * 6 faces)

    glm::vec3 _000 = {leftPlane, bottomPlane, nearPlane};  // Front-Left-Bottom
    glm::vec3 _100 = {rightPlane, bottomPlane, nearPlane}; // Front-Right-Bottom
    glm::vec3 _110 = {rightPlane, topPlane, nearPlane};    // Front-Right-Top
    glm::vec3 _010 = {leftPlane, topPlane, nearPlane};     // Front-Left-Top

    glm::vec3 _001 = {leftPlane, bottomPlane, farPlane};  // Back-Left-Bottom
    glm::vec3 _101 = {rightPlane, bottomPlane, farPlane}; // Back-Right-Bottom
    glm::vec3 _111 = {rightPlane, topPlane, farPlane};    // Back-Right-Top
    glm::vec3 _011 = {leftPlane, topPlane, farPlane};     // Back-Left-Top

    // Left-handed coordinate system: X+ right, Y+ up, Z+ forward (into screen)
    // Counter-clockwise (CCW) winding order as front-facing
    // Each face uses LB -> RB -> RT -> LT order, ensuring CCW when viewed from outside

    outVertices = {
        // Front face (Z = near, normal = -Z)
        // Viewed from outside (-Z direction)
        Vertex{.position = _000},
        Vertex{.position = _100},
        Vertex{.position = _110},
        Vertex{.position = _010},

        // Right face (X = right, normal = +X)
        // Viewed from outside (+X direction)
        Vertex{.position = _100},
        Vertex{.position = _101},
        Vertex{.position = _111},
        Vertex{.position = _110},

        // Top face (Y = top, normal = +Y)
        // Viewed from outside (+Y direction)
        Vertex{.position = _010},
        Vertex{.position = _110},
        Vertex{.position = _111},
        Vertex{.position = _011},

        // Left face (X = left, normal = -X)
        // Viewed from outside (-X direction)
        Vertex{.position = _001},
        Vertex{.position = _000},
        Vertex{.position = _010},
        Vertex{.position = _011},

        // Bottom face (Y = bottom, normal = -Y)
        // Viewed from outside (-Y direction)
        Vertex{.position = _001},
        Vertex{.position = _101},
        Vertex{.position = _100},
        Vertex{.position = _000},

        // Back face (Z = far, normal = +Z)
        // Viewed from outside (+Z direction)
        Vertex{.position = _101},
        Vertex{.position = _001},
        Vertex{.position = _011},
        Vertex{.position = _111},
    };

    // clang-format off
    outIndices = {
        0,  1,  2,  2,  3,  0,   // Front face
        4,  5,  6,  6,  7,  4,   // Right face
        8,  9,  10, 10, 11, 8,   // Top face
        12, 13, 14, 14, 15, 12,  // Left face
        16, 17, 18, 18, 19, 16,  // Bottom face
        20, 21, 22, 22, 23, 20   // Back face
    };
    // clang-format on

    // Apply transform to all vertex positions
    for (auto &vertex : outVertices) {
        // glm::vec4 transformedPos = transform * glm::vec4(vertex.position, 1.0f);
        // vertex.position          = glm::vec3(transformedPos);
        // 暂时不需要生成世界空间的mesh.... 捣乱
        vertex.position = vertex.position;
    }

    if (bGenTexcoords) {
        // uv lt = (0,0), lb = (0,1), rt = (1,0), rb = (1,1)
        // follow the vulkan but not opengl convention
        for (uint32_t i = 0; i < outVertices.size(); i += 4) {
            outVertices[i + 0].texCoord0 = {0.0f, 1.0f}; // LB
            outVertices[i + 1].texCoord0 = {1.0f, 1.0f}; // RB
            outVertices[i + 2].texCoord0 = {1.0f, 0.0f}; // RT
            outVertices[i + 3].texCoord0 = {0.0f, 0.0f}; // LT
        }
    }

    if (bGenNormals) {
        // Calculate normal matrix (inverse transpose of transform matrix)

        // WRONG: double  do this in cpu and shader sides
        // glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(transform)));

        // Face normals pointing outward from cube center
        // Left-handed system: Z+ forward (into screen), Y+ up, X+ right
        std::vector<glm::vec3> faceNormals = {
            glm::vec3(0.0f, 0.0f, -1.0f), // Front face (Z = near, normal points backward out of screen)
            glm::vec3(1.0f, 0.0f, 0.0f),  // Right face (normal points right)
            glm::vec3(0.0f, 1.0f, 0.0f),  // Top face (normal points up)
            glm::vec3(-1.0f, 0.0f, 0.0f), // Left face (normal points left)
            glm::vec3(0.0f, -1.0f, 0.0f), // Bottom face (normal points down)
            glm::vec3(0.0f, 0.0f, 1.0f)   // Back face (Z = far, normal points forward into screen)
        };

        // Apply normal matrix to transform normals and assign to vertices
        for (int face = 0; face < 6; ++face) {
            // glm::vec3 transformedNormal = glm::normalize(normalMatrix * faceNormals[face]);
            // glm::vec4 normal4           = glm::vec4(transformedNormal, 0.0f);
            glm::vec4 normal4 = glm::vec4(faceNormals[face], 0.0f);

            int baseIndex                     = face * 4;
            outVertices[baseIndex + 0].normal = normal4;
            outVertices[baseIndex + 1].normal = normal4;
            outVertices[baseIndex + 2].normal = normal4;
            outVertices[baseIndex + 3].normal = normal4;
        }
    }
}

// ===== 快速几何体生成器实现 =====

namespace PrimitiveGeometry
{

void createCube(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    createCube(glm::vec3(1.0f), outVertices, outIndices);
}

void createCube(const glm::vec3 &size, std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
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

void createSphere(float radius, uint32_t slices, uint32_t stacks,
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

void createPlane(float width, float depth, float uRepeat, float vRepeat,
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

void createCylinder(float radius, float height, uint32_t segments,
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
        outVertices.push_back({{x, -halfHeight, z}, {float(i) / segments, 0.0f}, normal});
        // Top vertex
        outVertices.push_back({{x, halfHeight, z}, {float(i) / segments, 1.0f}, normal});
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
    outVertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}});

    uint32_t topCenterIdx = outVertices.size();
    outVertices.push_back({{0.0f, halfHeight, 0.0f}, {0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}});

    for (uint32_t i = 0; i <= segments; ++i) {
        float theta = 2.0f * glm::pi<float>() * float(i) / float(segments);
        float x     = radius * cos(theta);
        float z     = radius * sin(theta);

        outVertices.push_back({{x, -halfHeight, z}, {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}, {0.0f, -1.0f, 0.0f}});
        outVertices.push_back({{x, halfHeight, z}, {0.5f + 0.5f * cos(theta), 0.5f + 0.5f * sin(theta)}, {0.0f, 1.0f, 0.0f}});
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

void createCone(float radius, float height, uint32_t segments,
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

void createFullscreenQuad(std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices)
{
    // NDC coordinates, suitable for post-processing
    outVertices = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}};

    outIndices = {0, 1, 2, 2, 3, 0};
}

} // namespace PrimitiveGeometry


} // namespace ya