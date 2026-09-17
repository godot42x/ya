#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/Core/EngineMeshData.h"

namespace ya
{

struct YA_RESOURCE_CORE_API PrimitiveGeometryFactory
{
    [[nodiscard]] static EngineMeshData createEngineMeshData(EPrimitiveGeometry type);
};

} // namespace ya
