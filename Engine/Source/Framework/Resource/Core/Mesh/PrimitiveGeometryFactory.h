#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/Core/EngineMeshData.h"

namespace ya
{

struct YA_RESOURCE_CORE_API PrimitiveGeometryFactory
{
    [[nodiscard]] static EngineMeshData createEngineMeshData(EPrimitiveGeometry type);
    /// Composite meshes the engine builds for itself. Built from the primitive
    /// shapes above plus rigid placement, so they cost no assets.
    [[nodiscard]] static EngineMeshData createEngineMeshData(EEngineMesh type);
};

} // namespace ya
