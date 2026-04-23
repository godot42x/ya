#include "Render/EngineGeometryNormalizer.h"

#include "Render/Model.h"

#include "Core/Log.h"

#include <algorithm>

namespace ya
{
namespace
{
const char* coordSystemToString(CoordinateSystem coordSystem)
{
    switch (coordSystem) {
    case CoordinateSystem::LeftHanded:
        return "LeftHanded";
    case CoordinateSystem::RightHanded:
        return "RightHanded";
    default:
        return "Unknown";
    }
}

bool needsCoordSystemNormalization(CoordinateSystem sourceCoordSystem)
{
    return sourceCoordSystem != ENGINE_COORDINATE_SYSTEM;
}

glm::vec3 flipHandedness(const glm::vec3& value)
{
    return {value.x, value.y, -value.z};
}

glm::vec3 normalizeDirectionIfNeeded(const glm::vec3& value)
{
    if (glm::dot(value, value) <= 0.0f) {
        return value;
    }
    return glm::normalize(value);
}

std::vector<ya::Vertex> buildEngineVertices(const ImportedMeshData& meshData, bool isNormalizeCoordSystem)
{
    std::vector<ya::Vertex> engineVertices;
    engineVertices.reserve(meshData.vertices.size());

    for (const ModelVertex& v : meshData.vertices) {
        ya::Vertex vertex;
        vertex.position  = isNormalizeCoordSystem ? flipHandedness(v.position) : v.position;
        vertex.normal    = isNormalizeCoordSystem ? normalizeDirectionIfNeeded(flipHandedness(v.normal)) : v.normal;
        vertex.texCoord0 = v.texCoord;
        vertex.tangent   = isNormalizeCoordSystem ? normalizeDirectionIfNeeded(flipHandedness(v.tangent)) : v.tangent;
        engineVertices.push_back(vertex);
    }

    return engineVertices;
}

std::vector<uint32_t> buildEngineIndices(const std::vector<uint32_t>& indices, bool normalizeCoordSystem)
{
    std::vector<uint32_t> engineIndices = indices;
    if (!normalizeCoordSystem) {
        return engineIndices;
    }

    YA_CORE_ASSERT(engineIndices.size() % 3 == 0,
                   "Triangle mesh indices must be divisible by 3, got {}",
                   engineIndices.size());

    for (size_t index = 0; index + 2 < engineIndices.size(); index += 3) {
        std::swap(engineIndices[index], engineIndices[index + 2]);
    }

    return engineIndices;
}

bool hasSkinningDataForMesh(const ImportedModelData& importedModelData, size_t meshIndex, size_t vertexCount)
{
    if (meshIndex >= importedModelData.meshBaseVertexIndex.size()) {
        return false;
    }

    const size_t baseVertexIndex = importedModelData.meshBaseVertexIndex[meshIndex];
    if (baseVertexIndex >= importedModelData.vertex2BoneData.size()) {
        return false;
    }

    const size_t lastVertexIndex = std::min(baseVertexIndex + vertexCount, importedModelData.vertex2BoneData.size());
    for (size_t globalVertexIndex = baseVertexIndex; globalVertexIndex < lastVertexIndex; ++globalVertexIndex) {
        if (!importedModelData.vertex2BoneData[globalVertexIndex].boneIDs.empty()) {
            return true;
        }
    }

    return false;
}

std::vector<ya::SkeletonMeshVertex> buildSkeletonVertices(const ImportedModelData& importedModelData, size_t meshIndex, size_t vertexCount)
{
    if (!hasSkinningDataForMesh(importedModelData, meshIndex, vertexCount)) {
        return {};
    }

    const size_t                        baseVertexIndex = importedModelData.meshBaseVertexIndex[meshIndex];
    std::vector<ya::SkeletonMeshVertex> skeletonVertices(vertexCount);

    for (size_t localVertexIndex = 0; localVertexIndex < vertexCount; ++localVertexIndex) {
        const size_t globalVertexIndex = baseVertexIndex + localVertexIndex;
        if (globalVertexIndex >= importedModelData.vertex2BoneData.size()) {
            break;
        }

        const auto& sourceBoneData = importedModelData.vertex2BoneData[globalVertexIndex];
        auto&       skeletonVertex = skeletonVertices[localVertexIndex];

        const size_t influenceCount = std::min<size_t>(MAX_BONE_WEIGHTS, std::min(sourceBoneData.boneIDs.size(), sourceBoneData.weights.size()));
        for (size_t influenceIndex = 0; influenceIndex < influenceCount; ++influenceIndex) {
            skeletonVertex.boneIDs[static_cast<int>(influenceIndex)] = static_cast<int>(sourceBoneData.boneIDs[influenceIndex]);
            skeletonVertex.weights[static_cast<int>(influenceIndex)] = sourceBoneData.weights[influenceIndex];
        }
    }

    return skeletonVertices;
}
} // namespace

EngineMeshData buildEngineMeshData(const ImportedModelData& importedModelData, size_t meshIndex)
{
    const ImportedMeshData& importedMesh             = importedModelData.meshes[meshIndex];
    const bool              needNormalizeCoordSystem = needsCoordSystemNormalization(importedMesh.sourceCoordSystem);

    if (needNormalizeCoordSystem) {
        YA_CORE_INFO("Normalize mesh '{}' from {} to {} during model creation",
                     importedMesh.name,
                     coordSystemToString(importedMesh.sourceCoordSystem),
                     coordSystemToString(ENGINE_COORDINATE_SYSTEM));
    }

    return EngineMeshData{
        .name             = importedMesh.name,
        .vertices         = buildEngineVertices(importedMesh, needNormalizeCoordSystem),
        .skeletonVertices = buildSkeletonVertices(importedModelData, meshIndex, importedMesh.vertices.size()),
        .indices          = buildEngineIndices(importedMesh.indices, needNormalizeCoordSystem),
    };
}

EngineMeshData buildEngineMeshData(std::string                         name,
                                   std::vector<ya::Vertex>             vertices,
                                   std::vector<uint32_t>               indices,
                                   std::vector<ya::SkeletonMeshVertex> skeletonVertices)
{
    return EngineMeshData{
        .name             = std::move(name),
        .vertices         = std::move(vertices),
        .skeletonVertices = std::move(skeletonVertices),
        .indices          = std::move(indices),
    };
}

} // namespace ya
