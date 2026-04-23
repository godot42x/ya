#include "PrimitiveMeshCache.h"
#include "PrimitiveGeometryFactory.h"

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
    if (!mesh) {
        return nullptr;
    }

    auto* meshPtr = mesh.get();
    _cache[type]  = std::move(mesh);
    return meshPtr;
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
    if (type == EPrimitiveGeometry::None) {
        return nullptr;
    }

    return Mesh::create(PrimitiveGeometryFactory::createEngineMeshData(type));
}

} // namespace ya
