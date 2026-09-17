#pragma once

#include "Core/Math/Geometry.h"
#include "Resource/Mesh.h"
#include "Core/ResourceRegistry.h"
#include <mutex>
#include <unordered_map>


namespace ya
{
struct IRender;

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
class YA_RESOURCE_API PrimitiveMeshCache : public IResourceCache
{
  public:
    static PrimitiveMeshCache &get();
    void setRender(IRender* render) { _render = render; }
    [[nodiscard]] IRender* getRender() const { return _render; }

    /**
     * @brief Get or create a primitive mesh
     * @param type The primitive geometry type
     * @return Shared pointer to the cached mesh, or nullptr if type is None
     *
     * Thread-safe: Multiple threads can call this concurrently
     */
    Mesh *getMesh(EPrimitiveGeometry type);

    /**
     * @brief Get or create a procedural engine mesh (companion / gizmo visuals)
     * @param type The engine mesh catalog entry
     * @return Shared pointer to the cached mesh, or nullptr if type is None
     *
     * Same lifetime and sharing rules as getMesh: keyed by the catalog value,
     * so two companions asking for the same body share one GPU mesh.
     */
    Mesh *getEngineMesh(EEngineMesh type);

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

    /**
     * @brief Check if an engine mesh is cached
     */
    bool hasEngineMesh(EEngineMesh type) const;

  private:
    PrimitiveMeshCache()  = default;
    ~PrimitiveMeshCache() = default;

    // Non-copyable
    PrimitiveMeshCache(const PrimitiveMeshCache &)            = delete;
    PrimitiveMeshCache &operator=(const PrimitiveMeshCache &) = delete;

    stdptr<Mesh> createMesh(EPrimitiveGeometry type);
    stdptr<Mesh> createEngineMesh(EEngineMesh type);

    IRender*                                             _render = nullptr;
    mutable std::mutex                                   _mutex;
    std::unordered_map<EPrimitiveGeometry, stdptr<Mesh>> _cache;
    std::unordered_map<EEngineMesh, stdptr<Mesh>>        _engineMeshCache;
};

} // namespace ya
