#include "Geometry.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>


namespace ya
{

void GeometryUtils::makeCube(
    float leftPlane, float rightPlane, float bottomPlane, float topPlane, float nearPlane, float farPlane,
    std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
    bool bGenTexcoords,
    bool bGenNormals, glm::mat4 transform)
{

    // to generate each vertex's normal or texcoord, we need not only 8 vertices
    // but 24 vertices (4 vertices per face * 6 faces)

    glm::vec3 FLB = {leftPlane, bottomPlane, nearPlane};
    glm::vec3 FRB = {rightPlane, bottomPlane, nearPlane};
    glm::vec3 FRT = {rightPlane, topPlane, nearPlane};
    glm::vec3 FLT = {leftPlane, topPlane, nearPlane};
    glm::vec3 BLB = {leftPlane, bottomPlane, farPlane};
    glm::vec3 BRB = {rightPlane, bottomPlane, farPlane};
    glm::vec3 BRT = {rightPlane, topPlane, farPlane};
    glm::vec3 BLT = {leftPlane, topPlane, farPlane};

    // always lb -> rb -> rt -> lt for each face (counter-clock wise from lb)

    outVertices = {
        // Front face
        Vertex{.position = FLB},
        Vertex{.position = FRB},
        Vertex{.position = FRT},
        Vertex{.position = FLT},

        // Right face
        Vertex{.position = FRB},
        Vertex{.position = BRB},
        Vertex{.position = BRT},
        Vertex{.position = FRT},

        // Top face
        Vertex{.position = FLT},
        Vertex{.position = FRT},
        Vertex{.position = BRT},
        Vertex{.position = BLT},

        // Left face
        Vertex{.position = BLB},
        Vertex{.position = FLB},
        Vertex{.position = FLT},
        Vertex{.position = BLT},

        // Bottom face
        Vertex{.position = BLB},
        Vertex{.position = BRB},
        Vertex{.position = FRB},
        Vertex{.position = FLB},

        // Back face
        Vertex{.position = BRB},
        Vertex{.position = BLB},
        Vertex{.position = BLT},
        Vertex{.position = BRT},
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
        glm::vec4 transformedPos = transform * glm::vec4(vertex.position, 1.0f);
        vertex.position          = glm::vec3(transformedPos);
    }



    if (bGenTexcoords) {
        for (int i = 0; i < outVertices.size(); i += 4) {
            outVertices[i + 0].texCoord0 = {0.0f, 0.0f}; // LB
            outVertices[i + 1].texCoord0 = {1.0f, 0.0f}; // RB
            outVertices[i + 2].texCoord0 = {1.0f, 1.0f}; // RT
            outVertices[i + 3].texCoord0 = {0.0f, 1.0f}; // LT
        }
    }

    if (bGenNormals) {
        // Calculate normal matrix (inverse transpose of transform matrix)
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(transform)));

        // Face normals in local space
        std::vector<glm::vec3> faceNormals = {
            glm::vec3(0.0f, 0.0f, -1.0f), // Front face
            glm::vec3(1.0f, 0.0f, 0.0f),  // Right face
            glm::vec3(0.0f, 1.0f, 0.0f),  // Top face
            glm::vec3(-1.0f, 0.0f, 0.0f), // Left face
            glm::vec3(0.0f, -1.0f, 0.0f), // Bottom face
            glm::vec3(0.0f, 0.0f, 1.0f)   // Back face
        };

        // Apply normal matrix to transform normals and assign to vertices
        for (int face = 0; face < 6; ++face) {
            glm::vec3 transformedNormal = glm::normalize(normalMatrix * faceNormals[face]);
            glm::vec4 normal4           = glm::vec4(transformedNormal, 0.0f);

            int baseIndex                     = face * 4;
            outVertices[baseIndex + 0].normal = normal4;
            outVertices[baseIndex + 1].normal = normal4;
            outVertices[baseIndex + 2].normal = normal4;
            outVertices[baseIndex + 3].normal = normal4;
        }
    }
}



} // namespace ya