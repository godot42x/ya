#pragma once

#include "Core/Math/Geometry.h"
#include "Render/Model/ImportedMeshData.h"
#include "Render/Model/ImportedSkeletonData.h"
#include "Render/Model/MaterialData.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct Model;

struct ImportedModelData
{
    CoordinateSystem sourceCoordSystem = ENGINE_COORDINATE_SYSTEM;

    std::string filepath;
    std::string directory;

    std::vector<ImportedMeshData>       meshes;
    std::vector<uint32_t>               meshBaseVertexIndex;
    std::vector<ImportedVertexBoneData> vertexBoneData;

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
