#pragma once

#include "Render/EngineMeshData.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ya
{

struct ImportedModelData;

EngineMeshData buildEngineMeshData(const ImportedModelData& importedModelData, size_t meshIndex);
EngineMeshData buildEngineMeshData(std::string                         name,
                                   std::vector<ya::Vertex>             vertices,
                                   std::vector<uint32_t>               indices,
                                   std::vector<ya::SkeletonMeshVertex> skeletonVertices = {});

} // namespace ya
