#pragma once

#include "Core/Math/Geometry.h"

#include <string>
#include <vector>

namespace ya
{

struct ImportedModelVertex
{
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 texCoord{};
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 tangent{};
};

struct ImportedMeshData
{
    std::vector<ImportedModelVertex> vertices;
    std::vector<uint32_t>            indices;
    CoordinateSystem                 sourceCoordSystem = CoordinateSystem::RightHanded;
    int32_t                          materialIndex     = -1;
    std::string                      name;
};

struct ImportedVertexBoneData
{
    std::vector<uint32_t> boneIDs;
    std::vector<float>    weights;
};

} // namespace ya
