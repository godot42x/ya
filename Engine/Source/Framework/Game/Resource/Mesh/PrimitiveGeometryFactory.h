#pragma once

#include "Foundation/Core/Math/Geometry.h"
#include "Framework/Game/Resource/EngineMeshData.h"

namespace ya
{

struct PrimitiveGeometryFactory
{
    [[nodiscard]] static EngineMeshData createEngineMeshData(EPrimitiveGeometry type);
};

} // namespace ya
