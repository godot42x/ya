#pragma once

#include "Core/Base.h"
#include "Core/Math/Geometry.h"
#include "Render/Model/MaterialData.h"

#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ya
{

struct Model;

struct ImportedModelVertex
{
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 texCoord{};
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 tangent{};
};

struct ImportedSkeletonNode
{
    FName                 name;
    uint32_t              parentIndex = INT_MAX;
    glm::mat4             localTransform{1.0f};
    glm::mat4             globalTransform{1.0f};
    std::vector<uint32_t> children;
};

struct ImportedSkeletonBoneInfo
{
    FName     name;
    glm::mat4 offsetMatrix{1.0f};
    uint32_t  boneIndex = 0;
    uint32_t  nodeIndex = INT_MAX;
};

struct ImportedSkeletonVectorKeyframe
{
    double    time = 0.0;
    glm::vec3 value{0.0f};
};

struct ImportedSkeletonQuatKeyframe
{
    double    time = 0.0;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

struct ImportedSkeletonAnimationChannel
{
    FName targetName;

    std::vector<ImportedSkeletonVectorKeyframe> positionKeys;
    std::vector<ImportedSkeletonQuatKeyframe>   rotationKeys;
    std::vector<ImportedSkeletonVectorKeyframe> scaleKeys;
};

struct ImportedSkeletonAnimationClip
{
    std::string                                   name;
    double                                        duration       = 0.0;
    double                                        ticksPerSecond = 0.0;
    std::vector<ImportedSkeletonAnimationChannel> channels;
};

struct ImportedMeshData
{
    std::vector<ImportedModelVertex> vertices;
    std::vector<uint32_t>            indices;
    CoordinateSystem                 sourceCoordSystem = CoordinateSystem::RightHanded;
    int32_t                          materialIndex     = -1;
    std::string                      name;
};

struct ImportedSkeletonData
{
    std::string                         name;
    std::unordered_map<FName, uint32_t> nameToNodeIndex;
    std::vector<ImportedSkeletonNode>   nodes;
    uint32_t                            rootNodeIndex = std::numeric_limits<uint32_t>::max();

    std::vector<ImportedSkeletonBoneInfo>      bones;
    std::unordered_map<FName, uint32_t>        boneNameToIndex;
    std::vector<ImportedSkeletonAnimationClip> animations;

    std::vector<uint32_t> meshIndices;

    bool isEmpty() const
    {
        return nodes.empty() && bones.empty() && animations.empty();
    }
};

struct ImportedModelData
{
    struct VertexBoneData
    {
        std::vector<uint32_t> boneIDs;
        std::vector<float>    weights;
    };

    CoordinateSystem sourceCoordSystem = ENGINE_COORDINATE_SYSTEM;

    std::string filepath;
    std::string directory;

    std::vector<ImportedMeshData> meshes;
    std::vector<uint32_t>         meshBaseVertexIndex;
    std::vector<VertexBoneData>   vertexBoneData;

    std::vector<ImportedSkeletonData> skeletons;
    std::vector<int32_t>              meshSkeletonIndices;

    std::vector<MaterialData> materials;
    std::vector<int32_t>      meshMaterialIndices;

    [[nodiscard]] bool isValid() const { return !meshes.empty(); }

    static ImportedModelData decode(const std::string& filepath);
    std::shared_ptr<Model>   createModel() const;


    bool hasSkinningDataForMesh(size_t meshIndex, size_t vertexCount) const
    {
        if (meshIndex >= meshBaseVertexIndex.size()) {
            return false;
        }

        const size_t baseVertexIndex = meshBaseVertexIndex[meshIndex];
        if (baseVertexIndex >= vertexBoneData.size()) {
            return false;
        }

        const size_t lastVertexIndex = std::min(baseVertexIndex + vertexCount, vertexBoneData.size());
        for (size_t globalVertexIndex = baseVertexIndex; globalVertexIndex < lastVertexIndex; ++globalVertexIndex) {
            if (!vertexBoneData[globalVertexIndex].boneIDs.empty()) {
                return true;
            }
        }

        return false;
    }
};

} // namespace ya
