#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/EngineMeshData.h"

namespace ya
{

struct PrimitiveGeometryFactory
{
    [[nodiscard]] static EngineMeshData createEngineMeshData(EPrimitiveGeometry type);
};

} // namespace ya
