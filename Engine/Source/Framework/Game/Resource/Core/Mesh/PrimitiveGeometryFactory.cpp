#include "PrimitiveGeometryFactory.h"

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
        return EngineMeshData{.name = "primitive_cube", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::Sphere:
        PrimitiveGeometry::createSphere(1.0f, 32, 16, vertices, indices);
        return EngineMeshData{.name = "primitive_sphere", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::Plane:
        PrimitiveGeometry::createPlane(1.0f, 1.0f, 1.0f, 1.0f, vertices, indices);
        return EngineMeshData{.name = "primitive_plane", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::Cylinder:
        PrimitiveGeometry::createCylinder(1.0f, 2.0f, 32, vertices, indices);
        return EngineMeshData{.name = "primitive_cylinder", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::Cone:
        PrimitiveGeometry::createCone(1.0f, 2.0f, 32, vertices, indices);
        return EngineMeshData{.name = "primitive_cone", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::Quad:
        PrimitiveGeometry::createFullscreenQuad(vertices, indices);
        return EngineMeshData{.name = "primitive_quad", .vertices = std::move(vertices), .skeletonVertices = {}, .indices = std::move(indices)};

    case EPrimitiveGeometry::None:
    default:
        YA_CORE_ASSERT(false, "Unsupported primitive geometry {}", static_cast<int>(type));
        return {};
    }
}

} // namespace ya
