#pragma once

#include "Core/Base.h"
#include "Core/Math/Geometry.h"

#include <string>
#include <vector>

namespace ya
{

struct EngineMeshData
{
    std::string                         name;
    std::vector<ya::Vertex>             vertices;
    std::vector<ya::SkeletonMeshVertex> skeletonVertices;
    std::vector<uint32_t>               indices;

    [[nodiscard]] bool hasSkinning() const { return !skeletonVertices.empty(); }
};

} // namespace ya
