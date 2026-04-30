#include "Render/EngineGeometryNormalizer.h"

#include "Render/Model.h"
#include "Render/Model/ImportedModelData.h"

#include "Core/Log.h"

#include <algorithm>

#include "Common.Limits.slang.h"
using ya::slang_types::Common::Limits::MAX_BONE_WEIGHT_PER_VERTEX;

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

std::vector<ya::Vertex> buildEngineVertices(const ImportedMeshData& meshData, bool needNormalizeCoordSystem)
{
    std::vector<ya::Vertex> engineVertices;
    engineVertices.reserve(meshData.vertices.size());

    for (const ImportedModelVertex& v : meshData.vertices) {
        ya::Vertex vertex;
        vertex.position  = needNormalizeCoordSystem ? flipHandedness(v.position) : v.position;
        vertex.normal    = needNormalizeCoordSystem ? normalizeDirectionIfNeeded(flipHandedness(v.normal)) : v.normal;
        vertex.texCoord0 = v.texCoord;
        vertex.tangent   = needNormalizeCoordSystem ? normalizeDirectionIfNeeded(flipHandedness(v.tangent)) : v.tangent;
        engineVertices.push_back(std::move(vertex));
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


std::vector<ya::SkeletonMeshVertex> buildSkeletonVertices(const ImportedModelData& importedModelData, size_t meshIndex, size_t vertexCount)
{
    if (!importedModelData.hasSkinningDataForMesh(meshIndex, vertexCount)) {
        return {};
    }

    const size_t baseVertexIndex = importedModelData.meshBaseVertexIndex[meshIndex];

    std::vector<ya::SkeletonMeshVertex> skeletonVertices(vertexCount);

    for (size_t localVertexIndex = 0; localVertexIndex < vertexCount; ++localVertexIndex) {
        const size_t globalVertexIndex = baseVertexIndex + localVertexIndex;
        if (globalVertexIndex >= importedModelData.vertexBoneData.size()) {
            break;
        }

        const auto& sourceBoneData = importedModelData.vertexBoneData[globalVertexIndex];
        auto&       skeletonVertex = skeletonVertices[localVertexIndex];
        skeletonVertex.boneIDs     = glm::ivec4(-1);
        skeletonVertex.weights     = glm::vec4(0.0f);

        YA_CORE_ASSERT(sourceBoneData.weights.size() == sourceBoneData.boneIDs.size(), "Bone IDs and weights size mismatch");

        size_t weightCount = std::min<uint32_t>(sourceBoneData.boneIDs.size(), MAX_BONE_WEIGHT_PER_VERTEX);
        for (size_t influenceIndex = 0; influenceIndex < weightCount; ++influenceIndex) {
            skeletonVertex.boneIDs[static_cast<int>(influenceIndex)] = static_cast<int32_t>(sourceBoneData.boneIDs[influenceIndex]);
            skeletonVertex.weights[static_cast<int>(influenceIndex)] = sourceBoneData.weights[influenceIndex];
        }
        for (size_t weightIdx = weightCount; weightIdx < MAX_BONE_WEIGHT_PER_VERTEX; ++weightIdx) {
            skeletonVertex.boneIDs[static_cast<int>(weightIdx)] = -1;
            skeletonVertex.weights[static_cast<int>(weightIdx)] = 0.0f;
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
