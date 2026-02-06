#include "PrimitiveMeshCache.h"
#include "Core/Log.h"

namespace ya
{

PrimitiveMeshCache &PrimitiveMeshCache::get()
{
    static PrimitiveMeshCache instance;
    return instance;
}

Mesh *PrimitiveMeshCache::getMesh(EPrimitiveGeometry type)
{
    if (type == EPrimitiveGeometry::None) {
        return nullptr;
    }

    // std::lock_guard<std::mutex> lock(_mutex);

    // Check if already cached
    auto it = _cache.find(type);
    if (it != _cache.end()) {
        return it->second.get();
    }

    // Create and cache
    auto mesh = createMesh(type);
    if (mesh) {
        _cache[type] = mesh;
    }
    return _cache[type].get();
}

void PrimitiveMeshCache::clearCache()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cache.clear();
    YA_CORE_INFO("PrimitiveMeshCache cleared");
}

bool PrimitiveMeshCache::hasMesh(EPrimitiveGeometry type) const
{
    // std::lock_guard<std::mutex> lock(_mutex);
    return _cache.contains(type);
}

stdptr<Mesh> PrimitiveMeshCache::createMesh(EPrimitiveGeometry type)
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    switch (type) {
    case EPrimitiveGeometry::Cube:
        PrimitiveGeometry::createCube(vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_cube");

    case EPrimitiveGeometry::Sphere:
        PrimitiveGeometry::createSphere(1.0f, 32, 16, vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_sphere");

    case EPrimitiveGeometry::Plane:
        PrimitiveGeometry::createPlane(1.0f, 1.0f, 1.0f, 1.0f, vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_plane");

    case EPrimitiveGeometry::Cylinder:
        PrimitiveGeometry::createCylinder(1.0f, 2.0f, 32, vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_cylinder");

    case EPrimitiveGeometry::Cone:
        PrimitiveGeometry::createCone(1.0f, 2.0f, 32, vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_cone");

    case EPrimitiveGeometry::Quad:
        PrimitiveGeometry::createFullscreenQuad(vertices, indices);
        return makeShared<Mesh>(vertices, indices, "primitive_quad");

    case EPrimitiveGeometry::None:
    default:
        return nullptr;
    }
}

} // namespace ya
