#include "Geometry.h"

void ya::GeometryUtils::createCube(
    float leftPlane, float rightPlane, float bottomPlane, float topPlane, float nearPlane, float farPlane,
    std::vector<Vertex> &outVertices, std::vector<uint32_t> &outIndices,
    bool bGenTexcoords, bool bGenNormals)
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



    if (bGenTexcoords) {
        for (int i = 0; i < outVertices.size(); i += 4) {
            outVertices[i + 0].texCoord0 = {0.0f, 1.0f};
            outVertices[i + 1].texCoord0 = {1.0f, 1.0f};
            outVertices[i + 2].texCoord0 = {1.0f, 0.0f};
            outVertices[i + 3].texCoord0 = {0.0f, 0.0f};
        }
    }

    if (bGenNormals) {
        // generate normal for each face from a view and model matrix
        for (int i = 0; i < outVertices.size(); i += 4) {
            UNIMPLEMENTED();
            // glm::vec3 v0     = outVertices[i + 1].position - outVertices[i + 0].position;
            // glm::vec3 v1     = outVertices[i + 3].position - outVertices[i + 0].position;
            // glm::vec3 normal = glm::normalize(glm::cross(v0, v1));

            // outVertices[i + 0].normal = glm::vec4(normal, 0.0f);
            // outVertices[i + 1].normal = glm::vec4(normal, 0.0f);
            // outVertices[i + 2].normal = glm::vec4(normal, 0.0f);
            // outVertices[i + 3].normal = glm::vec4(normal, 0.0f);
        }
    }
}
