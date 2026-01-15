#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ya
{

/**
 * @brief Interface for resource caches
 * All resource caches should implement this interface to be managed by ResourceRegistry
 */
struct IResourceCache
{
    virtual ~IResourceCache() = default;

    /**
     * @brief Clear all cached resources
     * Called during cleanup in priority order (higher priority first)
     */
    virtual void clearCache() = 0;

    /**
     * @brief Get the name of this cache for debugging
     */
    virtual const char *getCacheName() const = 0;
};

/**
 * @brief ResourceRegistry - Centralized management of all resource caches
 *
 * Provides unified lifecycle management for:
 * - AssetManager (models, textures)
 * - FontManager (fonts)
 * - TextureLibrary (default textures/samplers)
 * - PrimitiveMeshCache (primitive meshes)
 *
 * Usage:
 * @code
 * // During initialization (each cache registers itself)
 * ResourceRegistry::get().registerCache(&AssetManager::get(), 100);
 *
 * // During shutdown (clears all in priority order)
 * ResourceRegistry::get().clearAll();
 * @endcode
 */
class ResourceRegistry
{
  public:
    static ResourceRegistry &get()
    {
        static ResourceRegistry instance;
        return instance;
    }

    /**
     * @brief Register a resource cache
     * @param cache Pointer to the cache (must outlive the registry)
     * @param priority Higher values are cleared first (default: 0)
     */
    void registerCache(IResourceCache *cache, int priority = 0)
    {
        _caches.push_back({cache, priority});
        // Keep sorted by priority (descending)
        std::sort(_caches.begin(), _caches.end(), [](const auto &a, const auto &b) { return a.priority > b.priority; });
    }

    /**
     * @brief Clear all registered caches in priority order
     * Higher priority caches are cleared first
     */
    void clearAll();

    /**
     * @brief Check if any caches are registered
     */
    bool empty() const { return _caches.empty(); }

    /**
     * @brief Get number of registered caches
     */
    size_t size() const { return _caches.size(); }

  private:
    ResourceRegistry()  = default;
    ~ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry &)            = delete;
    ResourceRegistry &operator=(const ResourceRegistry &) = delete;

    struct CacheEntry
    {
        IResourceCache *cache;
        int             priority;
    };

    std::vector<CacheEntry> _caches;
};

} // namespace ya
