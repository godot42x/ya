#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/ResourceRegistry.h"
#include "Render/Mesh.h"
#include <mutex>
#include <unordered_map>

namespace ya
{

/**
 * @brief PrimitiveMeshCache - Singleton cache for primitive geometry meshes
 *
 * All primitive meshes (Cube, Sphere, Plane, etc.) are cached and shared
 * across all components that use them. This avoids:
 * - Redundant GPU buffer allocations
 * - Repeated geometry generation
 * - Synchronization issues when replacing meshes
 *
 * Usage:
 * @code
 * auto mesh = PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube);
 * @endcode
 */
class PrimitiveMeshCache : public IResourceCache
{
  public:
    static PrimitiveMeshCache &get();

    /**
     * @brief Get or create a primitive mesh
     * @param type The primitive geometry type
     * @return Shared pointer to the cached mesh, or nullptr if type is None
     *
     * Thread-safe: Multiple threads can call this concurrently
     */
    stdptr<Mesh> getMesh(EPrimitiveGeometry type);

    /**
     * @brief Clear all cached meshes (implements IResourceCache)
     * Call this before shutting down the renderer
     * @note Must ensure GPU is idle before calling
     */
    void  clearCache() override;
    FName getCacheName() const override { return "PrimitiveMeshCache"; }

    /**
     * @brief Check if a mesh is cached
     */
    bool hasMesh(EPrimitiveGeometry type) const;

  private:
    PrimitiveMeshCache()  = default;
    ~PrimitiveMeshCache() = default;

    // Non-copyable
    PrimitiveMeshCache(const PrimitiveMeshCache &)            = delete;
    PrimitiveMeshCache &operator=(const PrimitiveMeshCache &) = delete;

    stdptr<Mesh> createMesh(EPrimitiveGeometry type);

    mutable std::mutex                                   _mutex;
    std::unordered_map<EPrimitiveGeometry, stdptr<Mesh>> _cache;
};

} // namespace ya
