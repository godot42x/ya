#include "PrimitiveGeometryFactory.h"

#include "Core/Log.h"
#include "Resource/Core/Mesh/EngineMeshBuilder.h"

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

EngineMeshData PrimitiveGeometryFactory::createEngineMeshData(EEngineMesh type)
{
    switch (type) {
    case EEngineMesh::CameraBody: {
        // Engine space is right-handed with forward -Z and up +Y, so the lens
        // faces -Z. Dimensions are a small movie camera: a body with a lens
        // barrel, a hood, a viewfinder and two reels on top.
        EngineMeshBuilder builder("engine_mesh_camera_body");
        builder.box(glm::vec3(0.42f, 0.26f, 0.24f), glm::vec3(0.0f, 0.0f, 0.03f));
        builder.cylinder(0.10f, 0.16f, 24, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, -0.15f));
        builder.cylinder(0.125f, 0.045f, 24, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, -0.245f));
        builder.box(glm::vec3(0.09f, 0.08f, 0.09f), glm::vec3(0.0f, 0.155f, 0.05f));
        builder.cylinder(0.07f, 0.05f, 20, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-0.13f, 0.15f, 0.06f));
        builder.cylinder(0.07f, 0.05f, 20, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.13f, 0.15f, 0.06f));
        return std::move(builder.mesh);
    }

    case EEngineMesh::None:
    default:
        YA_CORE_ASSERT(false, "Unsupported engine mesh {}", static_cast<int>(type));
        return {};
    }
}

} // namespace ya
