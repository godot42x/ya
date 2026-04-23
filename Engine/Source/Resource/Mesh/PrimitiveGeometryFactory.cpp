#include "PrimitiveGeometryFactory.h"

#include "Render/EngineGeometryNormalizer.h"

#include "Core/Log.h"

namespace ya
{

EngineMeshData PrimitiveGeometryFactory::createEngineMeshData(EPrimitiveGeometry type)
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    switch (type) {
    case EPrimitiveGeometry::Cube:
        PrimitiveGeometry::createCube(vertices, indices);
        return buildEngineMeshData("primitive_cube", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::Sphere:
        PrimitiveGeometry::createSphere(1.0f, 32, 16, vertices, indices);
        return buildEngineMeshData("primitive_sphere", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::Plane:
        PrimitiveGeometry::createPlane(1.0f, 1.0f, 1.0f, 1.0f, vertices, indices);
        return buildEngineMeshData("primitive_plane", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::Cylinder:
        PrimitiveGeometry::createCylinder(1.0f, 2.0f, 32, vertices, indices);
        return buildEngineMeshData("primitive_cylinder", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::Cone:
        PrimitiveGeometry::createCone(1.0f, 2.0f, 32, vertices, indices);
        return buildEngineMeshData("primitive_cone", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::Quad:
        PrimitiveGeometry::createFullscreenQuad(vertices, indices);
        return buildEngineMeshData("primitive_quad", std::move(vertices), std::move(indices));

    case EPrimitiveGeometry::None:
    default:
        YA_CORE_ASSERT(false, "Unsupported primitive geometry {}", static_cast<int>(type));
        return {};
    }
}

} // namespace ya
